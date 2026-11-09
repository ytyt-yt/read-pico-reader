/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：将 ZIP 内的漫画图片按自然文件名排序，并适配现有单图阅读与进度。
 * English: Naturally sort comic images in ZIP and adapt them to image reading and progress.
 *
 * 冻结：只读、有界、不解析 ComicInfo 或网络资源；隐藏目录与元数据不参与页码。
 * Frozen: Read-only and bounded; no ComicInfo or network parsing; hidden paths/metadata are not pages.
 */
#include "book_cbz.h"
#include "book_cover.h"
#include "zip_reader.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CBZ_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
typedef struct {
    const char *name;
    int index;
    unsigned width, height;
    esp_err_t dimensions_error;
    bool dimensions_checked;
} cbz_page_t;
struct book_cbz {
    zip_reader_t *zip;
    cbz_page_t *pages;
    size_t count;
};

static bool page_name(const char *name) {
    for (const char *part = name; part && *part;) {
        if (*part == '.' || !strncmp(part, "__MACOSX/", 9)) return false;
        const char *slash = strchr(part, '/');
        part = slash ? slash + 1 : NULL;
    }
    const char *ext = strrchr(name, '.');
    return ext && (!strcasecmp(ext, ".webp") || !strcasecmp(ext, ".jpg") ||
                   !strcasecmp(ext, ".jpeg") || !strcasecmp(ext, ".png"));
}
static bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
static unsigned char lower(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int page_compare(const void *left, const void *right) {
    const char *a = ((const cbz_page_t *)left)->name;
    const char *b = ((const cbz_page_t *)right)->name;
    const char *original_a = a, *original_b = b;
    while (*a && *b) {
        if (digit(*a) && digit(*b)) {
            while (*a == '0') ++a;
            while (*b == '0') ++b;
            const char *end_a = a, *end_b = b;
            while (digit(*end_a)) ++end_a;
            while (digit(*end_b)) ++end_b;
            size_t na = (size_t)(end_a - a), nb = (size_t)(end_b - b);
            if (na != nb) return na < nb ? -1 : 1;
            int cmp = memcmp(a, b, na);
            if (cmp) return cmp;
            a = end_a; b = end_b;
        } else {
            unsigned char ca = lower((unsigned char)*a++), cb = lower((unsigned char)*b++);
            if (ca != cb) return ca < cb ? -1 : 1;
        }
    }
    if (*a || *b) return *a ? 1 : -1;
    // 数值与大小写相同时用原路径保证稳定、明确的顺序。/ Break numeric/case ties by the original path.
    return strcmp(original_a, original_b);
}

esp_err_t book_cbz_open(const char *path, book_cbz_t **out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!path || !*path) return ESP_ERR_INVALID_ARG;
    book_cbz_t *book = heap_caps_calloc(1, sizeof(*book), CBZ_CAPS);
    if (!book) return ESP_ERR_NO_MEM;
    esp_err_t err = zip_open(path, &book->zip);
    if (err != ESP_OK) goto fail;
    size_t entries = zip_entry_count(book->zip);
    for (size_t i = 0; i < entries; ++i) {
        if (!page_name(zip_entry_name(book->zip, (int)i))) continue;
        ++book->count;
    }
    if (!book->count) { err = ESP_ERR_NOT_SUPPORTED; goto fail; }
    book->pages = heap_caps_calloc(book->count, sizeof(*book->pages), CBZ_CAPS);
    if (!book->pages) { err = ESP_ERR_NO_MEM; goto fail; }
    size_t page = 0;
    for (size_t i = 0; i < entries; ++i) {
        const char *name = zip_entry_name(book->zip, (int)i);
        if (page_name(name)) book->pages[page++] = (cbz_page_t){.name = name, .index = (int)i};
    }
    qsort(book->pages, book->count, sizeof(*book->pages), page_compare);
    *out = book;
    return ESP_OK;
fail:
    book_cbz_close(book);
    return err;
}
void book_cbz_close(book_cbz_t *book) {
    if (!book) return;
    zip_close(book->zip);
    free(book->pages);
    free(book);
}
size_t book_cbz_count(const book_cbz_t *book) { return book ? book->count : 0; }
esp_err_t book_cbz_title(const book_cbz_t *book, size_t page, char *out, size_t cap) {
    if (!book || page >= book->count || !out || !cap) return ESP_ERR_INVALID_ARG;
    const char *name = strrchr(book->pages[page].name, '/');
    name = name ? name + 1 : book->pages[page].name;
    size_t len = strlen(name);
    if (len >= cap) { *out = 0; return ESP_ERR_INVALID_SIZE; }
    memcpy(out, name, len + 1);
    return ESP_OK;
}
static bool matches(const book_cbz_t *book, size_t page, const char *src) {
    return book && page < book->count && src && !strcmp(src, book->pages[page].name);
}
esp_err_t book_cbz_dimensions(book_cbz_t *book, size_t page, const char *src,
                              unsigned *width, unsigned *height) {
    if (!width || !height) return ESP_ERR_INVALID_ARG;
    *width = *height = 0;
    if (!matches(book, page, src)) return ESP_ERR_INVALID_ARG;
    cbz_page_t *entry = &book->pages[page];
    if (entry->dimensions_checked) {
        *width = entry->width; *height = entry->height;
        return entry->dimensions_error;
    }
    int index = entry->index;
    size_t size = zip_entry_size(book->zip, index);
    if (!size || size > ZIP_OUTPUT_MAX) return ESP_ERR_INVALID_SIZE;
    // 常见 WebP/PNG 尺寸只需短头；JPEG 的晚到帧头再扩大探测。
    // WebP/PNG need only a short header; expand probing only for late JPEG frame headers.
    size_t prefix = size < 512 ? size : 512;
    uint8_t *data = heap_caps_malloc(prefix, CBZ_CAPS);
    if (!data) return ESP_ERR_NO_MEM;
    esp_err_t err = zip_extract_prefix(book->zip, index, data, prefix);
    bool png = err == ESP_OK && prefix >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8);
    if (err == ESP_OK && !book_image_dimensions(data, prefix, png, width, height)) {
        bool jpeg = prefix >= 2 && data[0] == 0xff && data[1] == 0xd8;
        err = ESP_ERR_NOT_SUPPORTED;
        if (jpeg && size > prefix) {
            free(data);
            prefix = size < 65536 ? size : 65536;
            data = heap_caps_malloc(prefix, CBZ_CAPS);
            if (!data) return ESP_ERR_NO_MEM;
            err = zip_extract_prefix(book->zip, index, data, prefix);
            if (err == ESP_OK && !book_image_dimensions(data, prefix, false, width, height))
                err = ESP_ERR_NOT_SUPPORTED;
        }
    }
    if (err != ESP_OK) *width = *height = 0;
    free(data);
    if (err != ESP_ERR_NO_MEM && err != ESP_FAIL) {
        entry->width = *width; entry->height = *height;
        entry->dimensions_error = err; entry->dimensions_checked = true;
    }
    return err;
}
esp_err_t book_cbz_load(book_cbz_t *book, size_t page, html_text_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!book || page >= book->count) return ESP_ERR_INVALID_ARG;
    out->utf8 = heap_caps_malloc(2, CBZ_CAPS);
    out->blocks = heap_caps_calloc(1, sizeof(*out->blocks), CBZ_CAPS);
    out->images = heap_caps_calloc(1, sizeof(*out->images), CBZ_CAPS);
    if (!out->utf8 || !out->blocks || !out->images) goto no_mem;
    const char *name = book->pages[page].name;
    out->images[0] = heap_caps_malloc(strlen(name) + 1, CBZ_CAPS);
    if (!out->images[0]) goto no_mem;
    out->image_count = 1;
    strcpy(out->images[0], name);
    // 一个占位字节保留现有布局与进度契约，图片块消费它而不画文字。
    // One placeholder byte preserves layout/progress contracts; the image block consumes it without text.
    memcpy(out->utf8, "\n", 2);
    out->len = out->count = 1;
    unsigned width = 0, height = 0;
    (void)book_cbz_dimensions(book, page, name, &width, &height);
    out->blocks[0] = (blk_t){.len = 1, .image = 0, .image_width = width, .image_height = height};
    return ESP_OK;
no_mem:
    html_text_free(out);
    return ESP_ERR_NO_MEM;
}
static esp_err_t image_bounded(book_cbz_t *book, size_t page, const char *src,
                                uint8_t **data, size_t *size, bool *png, size_t budget) {
    if (!data || !size || !png) return ESP_ERR_INVALID_ARG;
    *data = NULL; *size = 0; *png = false;
    if (!matches(book, page, src)) return ESP_ERR_INVALID_ARG;
    int index = book->pages[page].index;
    size_t bytes = zip_entry_size(book->zip, index);
    if (!bytes || bytes > ZIP_OUTPUT_MAX) return ESP_ERR_INVALID_SIZE;
    if (bytes > budget) return ESP_ERR_NO_MEM;
    uint8_t *image = heap_caps_malloc(bytes, CBZ_CAPS);
    if (!image) return ESP_ERR_NO_MEM;
    esp_err_t err = zip_extract(book->zip, index, image, bytes);
    if (err != ESP_OK) { free(image); return err; }
    *png = bytes >= 8 && !memcmp(image, "\x89PNG\r\n\x1a\n", 8);
    *data = image; *size = bytes;
    return ESP_OK;
}
esp_err_t book_cbz_image(book_cbz_t *book, size_t page, const char *src,
                         uint8_t **data, size_t *size, bool *png) {
    return image_bounded(book, page, src, data, size, png, ZIP_OUTPUT_MAX);
}
esp_err_t book_cbz_page_image_bounded(book_cbz_t *book, size_t page,
                                      uint8_t **data, size_t *size, bool *png, size_t budget) {
    const char *src = book && page < book->count ? book->pages[page].name : NULL;
    return image_bounded(book, page, src, data, size, png, budget);
}
esp_err_t book_cbz_cover_bounded(const char *path, uint8_t **data, size_t *size,
                                 bool *png, size_t budget) {
    if (!data || !size || !png) return ESP_ERR_INVALID_ARG;
    *data = NULL; *size = 0; *png = false;
    book_cbz_t *book = NULL;
    esp_err_t err = book_cbz_open(path, &book);
    if (err == ESP_OK) err = image_bounded(book, 0, book->pages[0].name, data, size, png, budget);
    book_cbz_close(book);
    return err;
}

#if KIIKO_BOARD_METALIO47
esp_err_t book_cbz_suspend_io(book_cbz_t *book) {
    return book ? zip_suspend_io(book->zip) : ESP_OK;
}
esp_err_t book_cbz_resume_io(book_cbz_t *book, const char *path) {
    return book ? zip_resume_io(book->zip, path) : ESP_OK;
}
#endif
