/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：两类书架的共用样式与封面抽出反馈共用绘制原语。
 * English: Shared rendering for shelf styles and cover-lift feedback on both shelves.
 * 冻结：纯绘制；封面与点按几何保持现有书架约定。
 * Frozen: Paint only; preserve the established shelf cover and press geometry.
 * 用户要求漫画跟随当前main书架：共用标题按钮、翻页控件与内容起点，绘制和命中采用同一几何。
 * User aligns Comics with the current main shelf: share header buttons, paging controls and content origins with matching paint/hit geometry.
 */
#include "ui_shelf.h"
#include "book_cover.h"
#include "ttf_font.h"
#include "ui_image_dither.h"
#include <stdio.h>
#include <string.h>

int ui_shelf_rows(uint8_t style) { return style == 5 ? 4 : 9; }
EpdRect ui_shelf_row(uint8_t style, int row) {
    if (style == 5) return (EpdRect){36, 184 + row * 197, 612, 197};
    int cell = (ui_content_width() - 2 * UI_GAP) / 3;
    return (EpdRect){UI_MARGIN + row % 3 * (cell + UI_GAP), 192 + row / 3 * 282, cell, 260};
}
EpdRect ui_shelf_image(uint8_t style, int row) {
    EpdRect card = ui_shelf_row(style, row);
    return style == 5 ? (EpdRect){71, card.y + (197 - 156) / 2, 120, 156}
        : (EpdRect){card.x + (card.width - 164) / 2, card.y, 164, 214};
}
int ui_shelf_lift(uint8_t style) { return style == 5 ? 8 : 16; }
EpdRect ui_shelf_list_frame(void) { return (EpdRect){36, 184, 612, 788}; }
EpdRect ui_shelf_header_rect(int slot) {
    return slot == 0 ? (EpdRect){442, 94, 97, 54} :
           slot == 1 ? (EpdRect){551, 94, 97, 54} : (EpdRect){0};
}
void ui_shelf_header_button(uint8_t *fb, EpdRect rect, const char *label, bool pressed) {
    ui_fill_round_rect(fb, rect, 18, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, rect, 18, 0x70);
    if (pressed) ui_draw_pressed_round_rect(fb, rect, 18);
    ui_text_vc(fb, rect.x + rect.width / 2, rect.y + rect.height / 2 + (pressed ? 2 : 0),
               20, label, EPD_DRAW_ALIGN_CENTER, false);
}
EpdRect ui_shelf_feedback(uint8_t style, int row) {
    EpdRect area = ui_shelf_image(style, row);
    area.y -= ui_shelf_lift(style); area.height += ui_shelf_lift(style);
    return area;
}
void ui_shelf_cover_pixels(uint8_t *fb, EpdRect image, const uint8_t *gray, unsigned sw,
                           unsigned sh, const uint8_t *bits, bool fast) {
    if (bits) {
        ui_blit_mono_bitmap(fb, image, bits, UI_SHELF_FAST_STRIDE);
    } else if (gray && sw && sh) {
        book_crop_t crop = book_cover_crop(sw, sh, image.width, image.height);
        unsigned sample_x[164];
        uint8_t tones[256];
        const bool sampled = image.width <= 164;
        if (sampled) for (int x = 0; x < image.width; ++x)
            sample_x[x] = crop.x + (uint64_t)(unsigned)x * crop.width / (unsigned)image.width;
        for (unsigned value = 0; value < 256; ++value) tones[value] = ui_contrast_gray(value);
        for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
            unsigned sy = crop.y + (uint64_t)(unsigned)y * crop.height / (unsigned)image.height;
            unsigned sx = sampled ? sample_x[x] : crop.x + (uint64_t)(unsigned)x * crop.width / (unsigned)image.width;
            uint8_t tone = tones[gray[sy * sw + sx]];
            epd_draw_pixel(image.x + x, image.y + y, fast
                ? ui_image_dither_cover_bw((tone >> 4) * 17u, image.x + x, image.y + y)
                : ui_image_dither_gray(tone, image.x + x, image.y + y), fb);
        }
    }
}
void ui_shelf_furniture_at(uint8_t *fb, uint8_t style, bool fast, int origin) {
    if (!style || style == 5) return;
    for (int row = 0; row < 3; ++row) {
        int top = origin + row * 282;
        if (style == 1) {
            epd_fill_rect((EpdRect){36, top + 214, 612, 12}, 0x30, fb);
            ui_hairline(fb, top + 214, 36, 612, 0x58);
            ui_hairline(fb, top + 225, 36, 612, 0x10);
        } else if (style == 2) {
            EpdRect acrylic = {20, top + 136, 644, 90};
            ui_draw_acrylic_guard(fb, acrylic);
            if (fast) ui_acrylic_bw_rect(fb, acrylic);
        } else if (style == 6) {
            EpdRect glass = {20, top + 188, 644, 42};
            ui_draw_liquid_shelf_rail(fb, glass);
            if (fast) ui_acrylic_bw_rect(fb, glass);
        }
    }
}
void ui_shelf_furniture(uint8_t *fb, uint8_t style, bool fast) {
    ui_shelf_furniture_at(fb, style, fast, ui_shelf_row(style, 0).y);
}
static void fit(char *text, int px, int width) {
    while (text[0] && ui_text_fixed_width_px(px, text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}
void ui_shelf_list_title(uint8_t *fb, const char *name, int top) {
    char lines[2][128] = {{0}};
    int px = ttf_em_height_px(27);
    const unsigned char *at = (const unsigned char *)name;
    for (int line = 0; line < 2 && *at; ++line) {
        size_t used = 0;
        while (*at && used < sizeof(lines[line]) - 5) {
            unsigned n = *at < 128 ? 1 : (*at & 0xe0) == 0xc0 ? 2 : (*at & 0xf0) == 0xe0 ? 3 : 4;
            if (strlen((const char *)at) < n) break;
            memcpy(lines[line] + used, at, n); lines[line][used + n] = 0;
            if (ui_text_fixed_width_px(px, lines[line]) > 378) { lines[line][used] = 0; break; }
            used += n; at += n;
        }
        if (line == 1 && *at) fit(lines[line], px, 352);
        ui_text_fixed_vc(fb, 244, top + line * 35, px, lines[line], EPD_DRAW_ALIGN_LEFT, false);
    }
}
void ui_shelf_list_details_at(uint8_t *fb, EpdRect card, int row, const char *name,
                              const char *description, unsigned progress, const char *label) {
    ui_shelf_list_title(fb, name, card.y + 42);
    char detail[256]; snprintf(detail, sizeof(detail), "%s", description);
    int px = ttf_em_height_px(21); fit(detail, px, 378);
    int y = ui_text_fixed_width_px(ttf_em_height_px(27), name) > 378 ? 106 : 83;
    ui_text_fixed_ink_vc(fb, 244, card.y + y, px, detail, EPD_DRAW_ALIGN_LEFT, 0x60);
    char status[288]; snprintf(status, sizeof(status), "%s", label);
    int status_px = ttf_em_height_px(19); fit(status, status_px, 378);
    ui_text_fixed_vc(fb, 244, card.y + 143, status_px, status, EPD_DRAW_ALIGN_LEFT, false);
    if (progress > 100) progress = 100;
    EpdRect bar = {244, card.y + 163, 378, 11};
    ui_fill_round_rect(fb, bar, 5, 0xd0);
    if (progress) {
        EpdRect fill = bar; fill.width = bar.width * progress / 100;
        if (fill.width < 1) fill.width = 1;
        ui_fill_round_rect(fb, fill, 5, 0x40);
    }
    ui_draw_control_frame(fb, bar, 5, 0x50);
    if (row < 3) ui_draw_separator(fb, card.y + 196, 50, 584, 0x60);
}
void ui_shelf_list_details(uint8_t *fb, int row, const char *name, const char *description,
                           unsigned progress, const char *label) {
    ui_shelf_list_details_at(fb, ui_shelf_row(5, row), row, name, description, progress, label);
}
EpdRect ui_shelf_page_arrow(int direction) {
    return ui_pager_rect(direction < 0 ? UI_PAGER_PREVIOUS : UI_PAGER_NEXT);
}
void ui_shelf_pager_state(uint8_t *fb, int count, int page, int pages, const char *unit, bool key_mode, int pressed) {
    char label[64]; snprintf(label, sizeof(label), "%d%s · %02d/%02d", count, unit, page + 1, pages);
    ui_pager_draw(fb, label, page, pages, key_mode, pressed);
}
void ui_shelf_pager(uint8_t *fb, int count, int page, int pages, const char *unit) {
    ui_shelf_pager_state(fb, count, page, pages, unit, false, UI_PAGER_NONE);
}
