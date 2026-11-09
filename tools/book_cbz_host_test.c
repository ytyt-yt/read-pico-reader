/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：执行实际 CBZ 书源、布局与 WebP 解码，验证顺序、内存、灰阶和错误恢复。
 * English: Exercise real CBZ sources, layout and WebP decoding across order, memory, grayscale and recovery.
 * 冻结：仅供主机验证，输出留在调用者指定的测试目录。/ Frozen: Host validation only, outputs stay in the chosen test directory.
 */
#include "book_source.h"
#include "book_cbz.h"
#include "book_cover.h"
#include "book_layout.h"
#include "book_comic_cache.h"
#include "book_webp.h"
#include "zip_reader.h"
#include "freertos/task.h"
#include "ttf_font.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#undef free

typedef struct { const void *request_app; int64_t now_ms; } app_ctx_t;
typedef enum { APP_REDRAW_NONE, APP_REDRAW_DONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL } app_redraw_t;
static unsigned s_unsaved, s_checkpoint_count;
static int64_t s_last_turn_ms;
static void save_progress(void) { ++s_checkpoint_count; s_unsaved = 0; }
#include "comic_present_gate.h"

typedef struct { void *ptr; size_t bytes; } allocation_t;
static allocation_t allocations[32768];
static size_t live, peak, budget = 8u * 1024u * 1024u;
static int fail_after = -1;
static pthread_mutex_t s_heap_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint s_full_reads, s_prefix_reads;
esp_err_t cbz_test_extract_full_impl(zip_reader_t *, int, void *, size_t);
esp_err_t cbz_test_extract_prefix_impl(zip_reader_t *, int, void *, size_t);
esp_err_t zip_extract(zip_reader_t *zip, int entry, void *data, size_t capacity) {
    atomic_fetch_add(&s_full_reads, 1);
    return cbz_test_extract_full_impl(zip, entry, data, capacity);
}
esp_err_t zip_extract_prefix(zip_reader_t *zip, int entry, void *data, size_t capacity) {
    atomic_fetch_add(&s_prefix_reads, 1);
    return cbz_test_extract_prefix_impl(zip, entry, data, capacity);
}
int test_guide_segments, test_guide_first_y, test_guide_height;
uint8_t test_guide_gray;

static size_t tracked_size(void *ptr) {
    pthread_mutex_lock(&s_heap_mutex);
    size_t result = 0;
    for (size_t i = 0; i < 32768; ++i) if (allocations[i].ptr == ptr) { result = allocations[i].bytes; break; }
    pthread_mutex_unlock(&s_heap_mutex);
    return result;
}
void *heap_caps_malloc(size_t bytes, int caps) {
    (void)caps;
    pthread_mutex_lock(&s_heap_mutex);
    if (!fail_after || live > budget || bytes > budget - live) {
        pthread_mutex_unlock(&s_heap_mutex); return NULL;
    }
    if (fail_after > 0) --fail_after;
    void *ptr = malloc(bytes ? bytes : 1);
    if (!ptr) { pthread_mutex_unlock(&s_heap_mutex); return NULL; }
    for (size_t i = 0; i < 32768; ++i) if (!allocations[i].ptr) {
        allocations[i] = (allocation_t){ptr, bytes};
        live += bytes;
        if (live > peak) peak = live;
        pthread_mutex_unlock(&s_heap_mutex); return ptr;
    }
    abort();
}
void cbz_test_free(void *ptr) {
    if (!ptr) return;
    pthread_mutex_lock(&s_heap_mutex);
    for (size_t i = 0; i < 32768; ++i) if (allocations[i].ptr == ptr) {
        live -= allocations[i].bytes;
        allocations[i] = (allocation_t){0};
        break;
    }
    pthread_mutex_unlock(&s_heap_mutex);
    free(ptr);
}
void heap_caps_free(void *ptr) { cbz_test_free(ptr); }
void *heap_caps_calloc(size_t count, size_t bytes, int caps) {
    if (count && bytes > SIZE_MAX / count) return NULL;
    void *ptr = heap_caps_malloc(count * bytes, caps);
    if (ptr) memset(ptr, 0, count * bytes);
    return ptr;
}
void *heap_caps_realloc(void *ptr, size_t bytes, int caps) {
    if (!ptr) return heap_caps_malloc(bytes, caps);
    void *next = heap_caps_malloc(bytes, caps);
    if (next) {
        size_t old = tracked_size(ptr);
        memcpy(next, ptr, old < bytes ? old : bytes);
        cbz_test_free(ptr);
    }
    return next;
}
size_t heap_caps_get_free_size(int caps) {
    (void)caps; pthread_mutex_lock(&s_heap_mutex);
    size_t result = budget > live ? budget - live : 0;
    pthread_mutex_unlock(&s_heap_mutex); return result;
}
size_t heap_caps_get_largest_free_block(int caps) { return heap_caps_get_free_size(caps); }

// 图片页不绘文字，字体仅提供布局所需的契约。/ Image pages do not draw text; supply only the layout font contract.
int ttf_font_open_mem(uint8_t *data, size_t len, const char *label) {
    (void)len; (void)label; cbz_test_free(data); return -1;
}
void ttf_font_close_embedded(void) {}
int ttf_font_select(int slot) { (void)slot; return 0; }
int ttf_font_selected(void) { return 0; }
bool ttf_font_slot_ready(int slot) { return slot == 0; }
int ttf_text_width_px(int px, const char *text) { return (int)strlen(text) * px; }
int ttf_text_left_bearing_px(int px, const char *text) { (void)px; (void)text; return 0; }
int ttf_ascender_px(int px) { return px; }
static bool dimensions(void *ctx, int image, int *width, int *height) {
    const html_text_t *text = ctx;
    assert(image == 0 && text->count == 1);
    *width = (int)text->blocks[0].image_width;
    *height = (int)text->blocks[0].image_height;
    return *width > 0 && *height > 0;
}
static void save_gray(const char *dir, size_t page, unsigned width, unsigned height, const uint8_t *gray) {
    char path[1024];
    assert(snprintf(path, sizeof(path), "%s/%04u.pgm", dir, (unsigned)page + 1) < (int)sizeof(path));
    FILE *file = fopen(path, "wb"); assert(file);
    fprintf(file, "P5\n%u %u\n255\n", width, height);
    assert(fwrite(gray, 1, (size_t)width * height, file) == (size_t)width * height);
    assert(!fclose(file));
}
static void read_comic(int argc, char **argv) {
    const char *path = argv[2], *dir = argv[3];
    size_t count = (size_t)argc - 4;
    assert(book_open(path) == ESP_OK && book_kind() == BOOK_KIND_CBZ);
    book_set_embedded_fonts(false);
    assert(book_chapter_count() == count && book_navigation_count() == count);
    assert(book_total_bytes() == count);
    for (size_t i = 0; i < count; ++i) {
        char title[512];
        assert(book_navigation_chapter(i) == i && !book_navigation_anchor(i));
        assert(book_navigation_source_offset(i) == SIZE_MAX && book_chapter_byte_offset(i) == i);
        assert(book_navigation_title(i, title, sizeof(title)) == ESP_OK);
        const char *expected = strrchr(argv[i + 4], '/');
        expected = expected ? expected + 1 : argv[i + 4];
        assert(!strcmp(title, expected));
        assert(book_chapter_title(i, title, 1) == ESP_ERR_INVALID_SIZE && !title[0]);
        html_text_t text = {0}; size_t off = 99;
        assert(book_chapter_load_blocks_target(i, NULL, SIZE_MAX, &off, &text) == ESP_OK && !off);
        assert(text.len == 1 && text.utf8[1] == 0 && text.count == 1 && text.blocks[0].image == 0);
        assert(text.image_count == 1 && !strcmp(text.images[0], argv[i + 4]));
        unsigned sw, sh;
        unsigned full_before = atomic_load(&s_full_reads), prefix_before = atomic_load(&s_prefix_reads);
        assert(book_chapter_image_dimensions(i, text.images[0], &sw, &sh) == ESP_OK && sw && sh);
        assert(book_chapter_image_dimensions(i, text.images[0], &sw, &sh) == ESP_OK && sw && sh);
        assert(atomic_load(&s_full_reads) == full_before && atomic_load(&s_prefix_reads) == prefix_before);
        book_layout_set_images_visible(true);
        book_layout_set_chapter_lead(0, 0);
        book_layout_set_image_bleed_width(684);
        book_layout_set_image_dims(dimensions, &text);
        int index, y, w = 0, h = 0;
        for (int mode = 0; mode < 2; ++mode) {
            assert(book_layout_build_blocks(text.utf8, text.len, text.blocks, text.count,
                                             (EpdRect){0, 0, 684, mode ? 1204 : 924}, 48));
            assert(book_layout_page_count() == 1 && book_layout_page_image_count(0) == 1);
            assert(book_layout_page_image_at(0, 0, &index, &y, &w, &h));
            assert(index == 0 && y == ((mode ? 1204 : 924) - h) / 2 && w > 0 && w <= 684 && h > 0 && h <= (mode ? 1204 : 924));
            assert(llabs((long long)w * sh - (long long)h * sw) <= sw + sh);
        }
        uint8_t *encoded = NULL; size_t bytes = 0; bool png = false;
        assert(book_chapter_image(i, text.images[0], &encoded, &bytes, &png) == ESP_OK && encoded && bytes);
        assert(atomic_load(&s_full_reads) == full_before + 1);
        unsigned iw, ih;
        assert(book_image_dimensions(encoded, bytes, png, &iw, &ih) && iw == sw && ih == sh);
        uint8_t *gray = heap_caps_malloc((size_t)w * h + 16, 3); assert(gray);
        memset(gray, 0xa5, (size_t)w * h + 16);
        assert(book_image_grayscale(encoded, bytes, png, (unsigned)w, (unsigned)h, gray));
        for (size_t p = (size_t)w * h; p < (size_t)w * h + 16; ++p) assert(gray[p] == 0xa5);
        save_gray(dir, i, (unsigned)w, (unsigned)h, gray);
        if (book_image_is_webp(encoded, bytes)) {
            size_t previous = budget;
            budget = live + 128u * 1024u;
            assert(!book_image_grayscale(encoded, bytes, png, (unsigned)w, (unsigned)h, gray));
            budget = previous;
        }
        if (i == 0) {
            uint8_t *cover = NULL; size_t cover_bytes = 0; bool cover_png = false;
            assert(book_cbz_cover_bounded(path, &cover, &cover_bytes, &cover_png, bytes - 1) == ESP_ERR_NO_MEM);
            assert(!cover && !cover_bytes);
            assert(book_cbz_cover_bounded(path, &cover, &cover_bytes, &cover_png, bytes) == ESP_OK);
            assert(cover_bytes == bytes && cover_png == png && !memcmp(cover, encoded, bytes));
            assert(book_kind() == BOOK_KIND_CBZ && book_chapter_count() == count);
            cbz_test_free(cover);
        }
        cbz_test_free(encoded); cbz_test_free(gray);
        assert(book_chapter_image(i, "unrelated.webp", &encoded, &bytes, &png) == ESP_ERR_INVALID_ARG);
        assert(!encoded && !bytes);
        book_layout_free(); book_layout_set_image_dims(NULL, NULL);
        html_text_free(&text);
    }
    book_close(); book_close();
    assert(book_kind() == BOOK_KIND_TXT && !book_chapter_count() && !live);
    // 每个索引/块表分配都可能失败；恢复后不能残留句柄或测试堆。
    // Every index/block allocation can fail; recovery must leave no source or test heap behind.
    for (int failure = 0; failure < 35; ++failure) {
        fail_after = failure;
        if (book_open(path) == ESP_OK) {
            html_text_t text = {0};
            esp_err_t err = book_chapter_load_blocks(0, &text);
            assert(err == ESP_OK || err == ESP_ERR_NO_MEM);
            html_text_free(&text);
        }
        book_close(); assert(!live);
    }
    fail_after = -1;
    printf("PASS: %zu comic pages, natural order, source/layout/gray, covers, bounded PSRAM and OOM; peak %.2f MiB\n",
           count, peak / (1024.0 * 1024.0));
}
static void cache_comic(const char *path, size_t count) {
    assert(!live && book_comic_cache_open(path) == ESP_OK);
    // 为前台帧缓冲和字体保留4MiB，验证真实八兆预算下的预解码余量。
    // Reserve 4 MiB for foreground frames/fonts to exercise headroom under a real eight-MiB budget.
    void *foreground = heap_caps_malloc(4u * 1024u * 1024u, 3); assert(foreground);
    book_comic_bitmap_t previous = {0};
    unsigned bounded_fallbacks = 0;
    app_ctx_t ctx = {.now_ms = 3000};
    for (size_t page = 0; page < count; ++page) {
        unsigned before = atomic_load(&s_full_reads);
        unsigned saved_before = s_checkpoint_count;
        s_unsaved = 1;
        book_comic_cache_prefetch(page, 684, 1204);
        // 真实主循环的空闲显示回调不得取消已发出的预解码。
        // Actual idle present calls must leave submitted predecode alive.
        for (int tick = 0; tick < 128; ++tick) {
            assert(comic_test_present_gate(&ctx, APP_REDRAW_NONE));
            comic_test_idle_checkpoint(&ctx);
        }
        assert(comic_test_present_gate(&ctx, APP_REDRAW_DONE));
        // 非阻塞空闲存档检查不得取消解码，直到正常完成再收齐通知。
        // Nonblocking idle-checkpoint polls must preserve work and reap normal completion.
        unsigned polls = 0;
        if (page & 1) book_comic_cache_wait(page, 684, 1204);
        else while (book_comic_cache_busy()) { assert(++polls < 10000); vTaskDelay(1); }
        assert(!book_comic_cache_busy());
        comic_test_idle_checkpoint(&ctx);
        assert(!s_unsaved && s_checkpoint_count == saved_before + 1);
        book_comic_bitmap_t current = {0}, reverse = {0};
        if (!book_comic_cache_take(page, 684, 1204, &current)) {
            // 大PNG可能超过预解码保留余量；仅允许读取前的预算拒绝，实际解码失败仍报错。
            // Large PNG can exceed predecode headroom; allow only pre-read budget skips, not decode failures.
            assert(atomic_load(&s_full_reads) == before);
            size_t available = heap_caps_get_free_size(3);
            book_cbz_t *fallback = NULL; assert(book_cbz_open(path, &fallback) == ESP_OK);
            uint8_t *encoded = NULL; size_t bytes = 0; bool png = false;
            assert(book_cbz_page_image_bounded(fallback, page, &encoded, &bytes, &png, ZIP_OUTPUT_MAX) == ESP_OK);
            assert(available <= 1024u * 1024u + 684u * 1204u * 3u / 2u + bytes);
            unsigned width, height; assert(book_image_dimensions(encoded, bytes, png, &width, &height));
            if (width > 684) { height = (unsigned)((uint64_t)height * 684 / width); width = 684; }
            if (height > 1204) { width = (unsigned)((uint64_t)width * 1204 / height); height = 1204; }
            if (!width) width = 1;
            if (!height) height = 1;
            current = (book_comic_bitmap_t){.page = page, .max_width = 684, .max_height = 1204,
                                            .width = width, .height = height,
                                            .gray = heap_caps_malloc((size_t)width * height, 3)};
            assert(current.gray && book_image_grayscale(encoded, bytes, png, width, height, current.gray));
            cbz_test_free(encoded); book_cbz_close(fallback);
            ++bounded_fallbacks;
        }
        assert(current.gray && current.width && current.height);
        assert(atomic_load(&s_full_reads) == before + 1);
        assert(!book_comic_cache_take(page, 684, 924, &reverse));
        if (page) {
            book_comic_cache_put(&previous); assert(!previous.gray);
            before = atomic_load(&s_full_reads);
            assert(book_comic_cache_take(page - 1, 684, 1204, &reverse));
            assert(reverse.gray && atomic_load(&s_full_reads) == before);
            book_comic_cache_put(&reverse); assert(!reverse.gray);
        }
        previous = current;
    }
    cbz_test_free(previous.gray);
    book_comic_cache_close(); cbz_test_free(foreground);
    assert(!live && !cbz_host_tasks && !cbz_host_semaphores);
    for (int round = 0; round < 40; ++round) {
        assert(book_comic_cache_open(path) == ESP_OK);
        book_comic_cache_prefetch((size_t)round % count, 684, 1204);
        if (round & 1) comic_test_zoom_gate();
        else assert(!comic_test_present_gate(&ctx, APP_REDRAW_AREA));
        assert(!book_comic_cache_busy());
        book_comic_cache_close(); book_comic_cache_close();
        assert(!live && !cbz_host_tasks && !cbz_host_semaphores);
    }
    cbz_host_task_fail = 1;
    assert(book_comic_cache_open(path) == ESP_OK);
    book_comic_bitmap_t one = {.page = 0, .max_width = 684, .max_height = 1204,
                               .width = 1, .height = 1, .gray = heap_caps_malloc(1, 3)};
    assert(one.gray); book_comic_cache_put(&one); assert(!one.gray);
    assert(book_comic_cache_take(0, 684, 1204, &one)); cbz_test_free(one.gray);
    book_comic_cache_prefetch(0, 684, 1204); book_comic_cache_close();
    cbz_host_task_fail = 0;
    assert(!live && !cbz_host_tasks && !cbz_host_semaphores);
    for (int failure = 0; failure < 15; ++failure) {
        fail_after = failure;
        (void)book_comic_cache_open(path);
        book_comic_cache_close(); assert(!live && !cbz_host_tasks && !cbz_host_semaphores);
    }
    fail_after = -1;
    printf("Cache stress peak with 4 MiB foreground reserve: %.2f MiB\n", peak / (1024.0 * 1024.0));
    printf("Predecode budget skips with successful foreground decode: %u\n", bounded_fallbacks);
    puts("PASS: real async predecode survives idle present ticks, one full extraction, reverse-page hits without IO, geometry keys, 40 scan/cancel/join/close cycles and task/allocation failure cleanup");
}
int main(int argc, char **argv) {
    assert(argc >= 3);
    if (!strcmp(argv[1], "read")) {
        assert(argc > 4); read_comic(argc, argv); cache_comic(argv[2], (size_t)argc - 4);
    }
    else if (!strcmp(argv[1], "reject")) {
        assert(book_open(argv[2]) != ESP_OK);
        assert(book_kind() == BOOK_KIND_TXT && !book_chapter_count() && !live);
    } else {
        assert(book_open(argv[2]) == ESP_OK && book_chapter_count() == 1);
        html_text_t text = {0}; assert(book_chapter_load_blocks(0, &text) == ESP_OK);
        uint8_t *encoded = NULL; size_t size = 0; bool png = false;
        esp_err_t err = book_chapter_image(0, text.images[0], &encoded, &size, &png);
        if (!strcmp(argv[1], "bad-image") || !strcmp(argv[1], "decode-error")) {
            assert(err == ESP_OK);
            unsigned w, h; uint8_t gray[64];
            assert(book_image_dimensions(encoded, size, png, &w, &h) == !strcmp(argv[1], "decode-error"));
            assert(!book_image_grayscale(encoded, size, png, 8, 8, gray));
        } else assert(err == (!strcmp(argv[1], "crc") ? ESP_ERR_INVALID_CRC : ESP_ERR_INVALID_SIZE));
        cbz_test_free(encoded); html_text_free(&text); book_close(); assert(!live);
    }
    return 0;
}
