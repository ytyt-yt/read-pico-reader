/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：保存前页与预测页，后台仅读独立CBZ并解码灰阶；前台接管已完成位图。
 * English: Cache the previous and predicted pages; background work reads its own CBZ and decodes gray.
 *
 * 冻结：最多两屏缓存，预解码保留1MiB余量；取消后汇合，不强删运行中的任务。
 * Frozen: Two screens of cache, a 1 MiB predecode reserve; join cancellation, never delete active work.
 */
#include "book_comic_cache.h"
#include "book_cbz.h"
#include "book_cover.h"
#include "book_webp.h"
#include "zip_reader.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "esp_log.h"
#endif

#define COMIC_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define COMIC_SCREEN_PIXELS (684u * 1216u)
#define COMIC_RESERVE (1024u * 1024u)
typedef struct {
    book_comic_bitmap_t bitmap;
    uint32_t used;
    bool present;
} comic_slot_t;
static book_cbz_t *s_book;
static SemaphoreHandle_t s_mutex, s_done;
static TaskHandle_t s_task;
static comic_slot_t s_slots[2];
static book_comic_bitmap_t s_request;
static atomic_bool s_cancel;
// pending只由前台读写；done在后台释放锁和临时资源后发送。
// Only the foreground owns pending; the worker signals done after releasing locks and scratch.
static bool s_pending;
static uint32_t s_clock;

static bool same(const book_comic_bitmap_t *bitmap, size_t page, unsigned width, unsigned height) {
    return bitmap->page == page && bitmap->max_width == width && bitmap->max_height == height;
}
static void slot_free(comic_slot_t *slot) {
    free(slot->bitmap.gray);
    memset(slot, 0, sizeof(*slot));
}
static void store_locked(book_comic_bitmap_t *bitmap) {
    int selected = -1;
    for (int i = 0; i < 2; ++i) {
        if (s_slots[i].present && same(&s_slots[i].bitmap, bitmap->page,
                                       bitmap->max_width, bitmap->max_height)) {
            selected = i; break;
        }
        if (!s_slots[i].present) selected = i;
    }
    if (selected < 0) selected = s_slots[0].used < s_slots[1].used ? 0 : 1;
    slot_free(&s_slots[selected]);
    s_slots[selected] = (comic_slot_t){*bitmap, ++s_clock, true};
    bitmap->gray = NULL;
}
static bool canceled(void *context) {
    (void)context;
    return atomic_load(&s_cancel);
}
static void decode_request(book_comic_bitmap_t *bitmap) {
    size_t available = heap_caps_get_free_size(COMIC_CAPS);
    size_t output_bound = (size_t)bitmap->max_width * bitmap->max_height;
    size_t workspace = output_bound * 3 / 2;
    if (available <= COMIC_RESERVE + workspace || canceled(NULL)) return;
    size_t budget = available - COMIC_RESERVE - workspace;
    if (budget > ZIP_OUTPUT_MAX) budget = ZIP_OUTPUT_MAX;
    uint8_t *encoded = NULL;
    size_t size = 0;
    bool png = false;
#ifdef ESP_PLATFORM
    int64_t started = esp_timer_get_time(), read_done = started;
#endif
    esp_err_t err = book_cbz_page_image_bounded(s_book, bitmap->page,
                                                &encoded, &size, &png, budget);
#ifdef ESP_PLATFORM
    read_done = esp_timer_get_time();
#endif
    unsigned width = 0, height = 0;
    if (err != ESP_OK || canceled(NULL) ||
        !book_image_dimensions(encoded, size, png, &width, &height)) goto done;
    if (width > bitmap->max_width) {
        height = (unsigned)((uint64_t)height * bitmap->max_width / width);
        width = bitmap->max_width;
    }
    if (height > bitmap->max_height) {
        width = (unsigned)((uint64_t)width * bitmap->max_height / height);
        height = bitmap->max_height;
    }
    if (!width) width = 1;
    if (!height) height = 1;
    bitmap->gray = heap_caps_malloc((size_t)width * height, COMIC_CAPS);
    if (!bitmap->gray) goto done;
    bool ok = book_image_is_webp(encoded, size)
        ? book_webp_grayscale_cancelable(encoded, size, width, height, bitmap->gray, canceled, NULL)
        : book_image_grayscale(encoded, size, png, width, height, bitmap->gray);
    if (!ok || canceled(NULL)) { free(bitmap->gray); bitmap->gray = NULL; }
    else { bitmap->width = width; bitmap->height = height; }
done:
    free(encoded);
#ifdef ESP_PLATFORM
    ESP_LOGI("comic_cache", "predecode page=%u read=%lldms decode=%lldms ready=%d canceled=%d",
             (unsigned)bitmap->page + 1, (read_done - started) / 1000,
             (esp_timer_get_time() - read_done) / 1000, bitmap->gray != NULL, canceled(NULL));
#endif
}
static void worker(void *argument) {
    (void)argument;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        book_comic_bitmap_t result = s_request;
        decode_request(&result);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (!canceled(NULL)) store_locked(&result);
        xSemaphoreGive(s_mutex);
        free(result.gray);
        xSemaphoreGive(s_done);
    }
}
esp_err_t book_comic_cache_open(const char *path) {
    book_comic_cache_close();
    esp_err_t err = book_cbz_open(path, &s_book);
    if (err != ESP_OK) return err;
    s_mutex = xSemaphoreCreateMutex();
    s_done = xSemaphoreCreateBinary();
    if (!s_mutex || !s_done) {
        book_comic_cache_close(); return ESP_ERR_NO_MEM;
    }
    atomic_store(&s_cancel, false);
    // 任务失败仍保留前页缓存，普通同步阅读不依赖它。
    // Retain previous-page caching if task creation fails; synchronous reading does not depend on it.
    if (xTaskCreatePinnedToCore(worker, "comic_prep", 12 * 1024, NULL, 1, &s_task, 1) != pdPASS) {
        s_task = NULL;
#ifdef ESP_PLATFORM
        ESP_LOGW("comic_cache", "worker unavailable; retaining previous-page cache only");
#endif
    }
    return ESP_OK;
}
void book_comic_cache_pause(void) {
    atomic_store(&s_cancel, true);
    if (s_pending) { xSemaphoreTake(s_done, portMAX_DELAY); s_pending = false; }
}
bool book_comic_cache_busy(void) {
    if (s_pending && xSemaphoreTake(s_done, 0)) s_pending = false;
    return s_pending;
}
void book_comic_cache_wait(size_t page, unsigned max_width, unsigned max_height) {
    if (!s_pending) return;
    if (!same(&s_request, page, max_width, max_height)) atomic_store(&s_cancel, true);
    xSemaphoreTake(s_done, portMAX_DELAY);
    s_pending = false;
}
void book_comic_cache_close(void) {
    book_comic_cache_pause();
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    if (s_mutex) { vSemaphoreDelete(s_mutex); s_mutex = NULL; }
    if (s_done) { vSemaphoreDelete(s_done); s_done = NULL; }
    for (int i = 0; i < 2; ++i) slot_free(&s_slots[i]);
    book_cbz_close(s_book); s_book = NULL;
    s_clock = 0;
}
bool book_comic_cache_take(size_t page, unsigned max_width, unsigned max_height,
                           book_comic_bitmap_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool found = false;
    for (int i = 0; i < 2; ++i) {
        if (s_slots[i].present && s_slots[i].bitmap.gray &&
            same(&s_slots[i].bitmap, page, max_width, max_height)) {
            *out = s_slots[i].bitmap;
            memset(&s_slots[i], 0, sizeof(s_slots[i]));
            found = true; break;
        }
    }
    xSemaphoreGive(s_mutex);
    return found;
}
void book_comic_cache_put(book_comic_bitmap_t *bitmap) {
    if (!bitmap || !bitmap->gray) return;
    if (!s_mutex || !bitmap->width || !bitmap->height ||
        (uint64_t)bitmap->width * bitmap->height > COMIC_SCREEN_PIXELS) {
        free(bitmap->gray); bitmap->gray = NULL; return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    store_locked(bitmap);
    xSemaphoreGive(s_mutex);
}
void book_comic_cache_prefetch(size_t page, unsigned max_width, unsigned max_height) {
    if (!s_task || !s_book || page >= book_cbz_count(s_book) || !max_width || !max_height ||
        max_width > 1216 || max_height > 1216 ||
        (uint64_t)max_width * max_height > COMIC_SCREEN_PIXELS) return;
    if (s_pending) {
        if (book_comic_cache_busy()) return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool present = false;
    for (int i = 0; i < 2; ++i) {
        if (s_slots[i].present && same(&s_slots[i].bitmap, page, max_width, max_height)) present = true;
    }
    // 两槽已满时先回收更早的旧页，不能等解码完成才腾出工作区。
    // Reclaim the older page before decoding when both slots are full, leaving workspace available.
    if (!present && s_slots[0].present && s_slots[1].present) {
        int oldest = s_slots[0].used < s_slots[1].used ? 0 : 1;
        slot_free(&s_slots[oldest]);
    }
    xSemaphoreGive(s_mutex);
    if (present || heap_caps_get_free_size(COMIC_CAPS) <= COMIC_RESERVE) return;
    s_request = (book_comic_bitmap_t){.page = page, .max_width = max_width, .max_height = max_height};
    atomic_store(&s_cancel, false);
    s_pending = true;
    xTaskNotifyGive(s_task);
}

#if KIIKO_BOARD_METALIO47
esp_err_t book_comic_cache_suspend_io(void) {
    book_comic_cache_pause();
    return book_cbz_suspend_io(s_book);
}
void book_comic_cache_resume_io(const char *path) {
    if (book_cbz_resume_io(s_book, path) != ESP_OK) book_comic_cache_close();
}
#endif
