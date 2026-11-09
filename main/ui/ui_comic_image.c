/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：把漫画逐像素绘制改成原生行的双像素写入；查表复用原有16级灰阶抖动。
 * English: Paint comic pairs in native rows with a lookup of the existing 16-gray dither.
 *
 * 冻结：四种旋转与原epd_draw_pixel逐像素目标一致，文字及EPUB插图不走本路径。
 * Frozen: Match epd_draw_pixel targets in all four rotations; text and EPUB images retain their paths.
 */
#include "ui_comic_image.h"
#include "ui_image_dither.h"
#include "epdiy.h"
#include <stdbool.h>
#include <stddef.h>

static uint8_t s_tone[16][256];
static bool s_tone_ready;
static inline uint8_t tone(const uint8_t *gray, ptrdiff_t at, int x, int y) {
    return s_tone[((unsigned)y & 3u) * 4u + ((unsigned)x & 3u)][gray[at]];
}
void ui_comic_image_paint(uint8_t *framebuffer, const uint8_t *gray,
                           int width, int height, int left, int top) {
    if (!framebuffer || !gray || width <= 0 || height <= 0 || width > 1216 || height > 1216)
        return;
    if (!s_tone_ready) {
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
            for (int value = 0; value < 256; ++value)
                s_tone[y * 4 + x][value] = ui_image_dither_gray((uint8_t)value, x, y) >> 4;
        s_tone_ready = true;
    }
    const int panel_w = epd_width(), panel_h = epd_height();
    const enum EpdRotation rotation = epd_get_rotation();
    int x0, y0, x1, y1;
    switch (rotation) {
        case EPD_ROT_LANDSCAPE:
            x0 = left; y0 = top; x1 = left + width; y1 = top + height; break;
        case EPD_ROT_PORTRAIT:
            x0 = panel_w - top - height; y0 = left;
            x1 = panel_w - top; y1 = left + width; break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            x0 = panel_w - left - width; y0 = panel_h - top - height;
            x1 = panel_w - left; y1 = panel_h - top; break;
        default:
            x0 = top; y0 = panel_h - left - width;
            x1 = top + height; y1 = panel_h - left; break;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > panel_w) x1 = panel_w;
    if (y1 > panel_h) y1 = panel_h;
    if (x0 >= x1 || y0 >= y1) return;
    for (int row = y0; row < y1; ++row) {
        int lx, ly, dx, dy;
        ptrdiff_t step;
        switch (rotation) {
            case EPD_ROT_LANDSCAPE:
                lx = x0; ly = row; dx = 1; dy = 0; step = 1; break;
            case EPD_ROT_PORTRAIT:
                lx = row; ly = panel_w - x0 - 1; dx = 0; dy = -1; step = -width; break;
            case EPD_ROT_INVERTED_LANDSCAPE:
                lx = panel_w - x0 - 1; ly = panel_h - row - 1;
                dx = -1; dy = 0; step = -1; break;
            default:
                lx = panel_h - row - 1; ly = x0; dx = 0; dy = 1; step = width; break;
        }
        ptrdiff_t at = (ptrdiff_t)(ly - top) * width + lx - left;
        uint8_t *dest = framebuffer + (size_t)row * (panel_w / 2) + x0 / 2;
        int column = x0;
        if (column & 1) {
            *dest = (*dest & 0x0f) | (tone(gray, at, lx, ly) << 4);
            ++dest; ++column; at += step; lx += dx; ly += dy;
        }
        for (; column + 1 < x1; column += 2) {
            uint8_t low = tone(gray, at, lx, ly);
            at += step; lx += dx; ly += dy;
            uint8_t high = tone(gray, at, lx, ly);
            at += step; lx += dx; ly += dy;
            *dest++ = low | (high << 4);
        }
        if (column < x1) *dest = (*dest & 0xf0) | tone(gray, at, lx, ly);
    }
}
