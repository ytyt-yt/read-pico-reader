/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：用 libwebp 的缩放亮度平面直接生成灰阶，分段解码时让出 CPU。
 * English: Decode scaled libwebp luma directly into grayscale, yielding between input segments.
 * 冻结：不建立源尺寸 RGB 平面；工作区用 PSRAM，不缓存解码器。
 * Frozen: No source-sized RGB plane; PSRAM workspaces; no retained decoder.
 */
#include "book_webp.h"
#include "book_cover.h"
#include "zip_reader.h"
#include "esp_heap_caps.h"
#include "src/webp/decode.h"
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#endif

bool book_webp_grayscale(const uint8_t *data, size_t size,
                          unsigned width, unsigned height, uint8_t *out) {
    return book_webp_grayscale_cancelable(data, size, width, height, out, NULL, NULL);
}
bool book_webp_grayscale_cancelable(const uint8_t *data, size_t size,
                                     unsigned width, unsigned height, uint8_t *out,
                                     book_image_cancel_fn cancel, void *context) {
    if (cancel && cancel(context)) return false;
    if (!data || !out || !size || size > ZIP_OUTPUT_MAX || !width || !height ||
        width > 1216 || height > 1216 || (uint64_t)width * height > 684u * 1216u) return false;
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config) ||
        WebPGetFeatures(data, size, &config.input) != VP8_STATUS_OK || config.input.has_animation ||
        config.input.width <= 0 || config.input.height <= 0 ||
        config.input.width > 8192 || config.input.height > 8192 ||
        (uint64_t)config.input.width * config.input.height > 16u * 1024u * 1024u) return false;
    size_t pixels = (size_t)width * height;
    size_t uv = (size_t)((width + 1) / 2) * ((height + 1) / 2);
    size_t scratch_size = uv * 2 + (config.input.has_alpha ? pixels : 0);
    uint8_t *scratch = heap_caps_malloc(scratch_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scratch) return false;
    config.output.colorspace = config.input.has_alpha ? MODE_YUVA : MODE_YUV;
    config.output.is_external_memory = 1;
    WebPYUVABuffer *plane = &config.output.u.YUVA;
    plane->y = out; plane->y_size = pixels; plane->y_stride = (int)width;
    plane->u = scratch; plane->v = scratch + uv;
    plane->u_size = plane->v_size = uv;
    plane->u_stride = plane->v_stride = (int)((width + 1) / 2);
    if (config.input.has_alpha) {
        plane->a = scratch + uv * 2; plane->a_size = pixels; plane->a_stride = (int)width;
    }
    book_crop_t crop = book_cover_crop((unsigned)config.input.width, (unsigned)config.input.height,
                                       width, height);
    config.options.use_cropping = 1;
    config.options.crop_left = (int)crop.x; config.options.crop_top = (int)crop.y;
    config.options.crop_width = (int)crop.width; config.options.crop_height = (int)crop.height;
    config.options.use_scaling = 1;
    config.options.scaled_width = (int)width; config.options.scaled_height = (int)height;
    WebPIDecoder *decoder = WebPIDecode(NULL, 0, &config);
    VP8StatusCode status = VP8_STATUS_SUSPENDED;
#ifdef ESP_PLATFORM
    int64_t yielded = esp_timer_get_time();
#endif
    if (decoder) {
        size_t received = 0;
        while (received < size && status == VP8_STATUS_SUSPENDED) {
            if (cancel && cancel(context)) break;
            size_t chunk = size - received;
            if (chunk > 4096) chunk = 4096;
            received += chunk;
            // 零拷贝追加可见前缀，压缩数据在调用结束前始终存活。/ Extend a zero-copy prefix while the caller keeps the input alive.
            status = WebPIUpdate(decoder, data, received);
#ifdef ESP_PLATFORM
            int64_t now = esp_timer_get_time();
            if (now - yielded >= 8000) { vTaskDelay(1); yielded = esp_timer_get_time(); }
#endif
        }
        WebPIDelete(decoder);
    }
    bool ok = decoder && status == VP8_STATUS_OK && !(cancel && cancel(context));
    // 亮度换算只计算256个输入值；不在每个像素上重复除法。
    // Compute the 256 luma values once instead of dividing for every pixel.
    uint8_t luma[256];
    for (int value = 0; value < 256; ++value) {
        int gray = (value - 16) * 255 + 109;
        gray = gray < 0 ? 0 : gray / 219;
        luma[value] = (uint8_t)(gray > 255 ? 255 : gray);
    }
    if (ok) for (size_t i = 0; i < pixels; ++i) {
        // WebP 的亮度范围 16–235 映射到灰阶全范围，再合成白底。/ Expand WebP luma 16–235 to full grayscale, then composite onto white.
        unsigned gray = luma[out[i]];
        if (config.input.has_alpha) {
            unsigned alpha = plane->a[i];
            gray = (gray * alpha + 255u * (255 - alpha) + 127) / 255;
        }
        out[i] = (uint8_t)gray;
        if ((i & 32767) == 0) {
            if (cancel && cancel(context)) { ok = false; break; }
#ifdef ESP_PLATFORM
            int64_t now = esp_timer_get_time();
            if (now - yielded >= 8000) { vTaskDelay(1); yielded = esp_timer_get_time(); }
#endif
        }
    }
    WebPFreeDecBuffer(&config.output);
    free(scratch);
    return ok;
}
