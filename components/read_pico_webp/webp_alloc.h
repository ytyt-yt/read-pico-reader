/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：仅替换 libwebp 内部堆分配，保留系统余量与上游溢出检查。
 * English: Replace only libwebp heap allocation, retaining system headroom and upstream overflow checks.
 * 冻结：解码工作区仅用 PSRAM；不足即返回失败，不挤占内部堆。
 * Frozen: Decoder workspaces use PSRAM only; fail without consuming internal heap.
 */
#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define WEBP_PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
static inline void *pico_webp_malloc(size_t bytes) {
    size_t available = heap_caps_get_free_size(WEBP_PSRAM_CAPS);
    if (available <= 192u * 1024u || bytes > available - 192u * 1024u) return NULL;
    return heap_caps_malloc(bytes, WEBP_PSRAM_CAPS);
}
static inline void *pico_webp_calloc(size_t count, size_t bytes) {
    if (count && bytes > SIZE_MAX / count) return NULL;
    size_t total = count * bytes;
    void *ptr = pico_webp_malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}
#define malloc pico_webp_malloc
#define calloc pico_webp_calloc
#endif
