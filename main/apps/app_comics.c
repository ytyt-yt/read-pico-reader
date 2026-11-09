/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：独立漫画/章节两层书架，共用文字书架样式，交给唯一阅读器打开CBZ。
 * English: Independent series/chapter shelves sharing text-shelf styles and opening CBZ in the sole reader.
 * 冻结：固定comics目录；当前层搜索，章节永远自然顺序；返回保持层级和页码，render纯绘图。
 * Frozen: Fixed comics root, search only this layer, chapters always natural order; retain return level/page and paint-only render.
 * 用户要求同步当前书架：标题按钮/翻页控件/内容起点共用，取消旧题头横线；按钮仅局部反馈。
 * User aligns the current shelf: share header/paging controls and content origins, remove the old header rule and keep button feedback local.
 * 本次跟随main：纯白底、四向分页与三键分页模式、后台封面及二级页底栏/右滑返回。
 * Follow main: white backgrounds, four-way paging and key paging mode, background covers, persistent child navigation and swipe-back.
 */
#include "app.h"
#include "app_content_open.h"
#include "app_registry.h"
#include "book_comics.h"
#include "book_cover.h"
#include "book_cover_jobs.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "read_pico_sd.h"
#include "read_pico_search.h"
#include "settings.h"
#include "ui_gesture.h"
#include "ui_image_dither.h"
#include "ui_keyboard.h"
#include "ui_nav.h"
#include "ui_menu.h"
#include "ui_shelf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static book_comic_catalog_t s_catalog;
static char s_directory[BOOK_STORE_PATH_MAX], s_requested_directory[BOOK_STORE_PATH_MAX];
static char s_message[128], s_requested_message[128];
static char s_query[65], s_root_query[65], s_draft[65];
static ui_text_edit_t s_input;
static bool s_search, s_scan_pending, s_request;
static int s_page, s_root_page, s_requested_page, s_pressed = -1;
static uint8_t s_style;
static unsigned s_revision, s_cover_generation;
static bool s_key_mode;
static int s_key_delta, s_key_pressed;
static int64_t s_key_until;
static int64_t s_poll;
static EpdRect s_area;
static enum EpdDrawMode s_feedback_mode;
static bool s_feedback, s_page_transition;
static struct {
    uint8_t *gray, *bits;
    uint64_t fingerprint;
    size_t index;
    bool ready;
} s_covers[9];

static void copy(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) { n = cap - 1; while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n; }
    memcpy(dst, src, n); dst[n] = 0;
}
static void fit(char *text, int px, int width) {
    while (text[0] && ui_text_fixed_width_px(ui_text_effective_px(px), text) > width) {
        size_t n = strlen(text) - 1;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}
static void drop_covers(void) {
    book_cover_jobs_cancel(s_cover_generation); s_cover_generation = 0;
    for (unsigned i = 0; i < 9; ++i) {
        free(s_covers[i].gray); free(s_covers[i].bits);
    }
    memset(s_covers, 0, sizeof(s_covers));
}
void app_comics_cover_mode_changed(void) { drop_covers(); }
static bool matches(size_t i) { return read_pico_search_match(s_catalog.entries[i].name, s_query); }
static size_t count(void) {
    size_t n = 0; for (size_t i = 0; i < s_catalog.count; ++i) if (matches(i)) ++n;
    return n;
}
static int pages(void) { size_t n = count(); return n ? (int)(1 + (n - 1) / ui_shelf_rows(s_style)) : 1; }
static size_t index_at(int row) {
    size_t target = (size_t)s_page * ui_shelf_rows(s_style) + row;
    for (size_t i = 0; i < s_catalog.count; ++i) if (matches(i)) {
        if (!target) return i;
        --target;
    }
    return SIZE_MAX;
}
static void clamp(void) { if (s_page >= pages()) s_page = pages() - 1; if (s_page < 0) s_page = 0; }
static void scan(void) {
    book_comics_dispose(&s_catalog); drop_covers();
    read_pico_sd_info_t sd = {0};
    if (read_pico_sd_get_info(&sd) == ESP_ERR_NOT_FINISHED) { s_scan_pending = true; return; }
    s_scan_pending = false;
    if (!sd.present || !sd.mounted) { copy(s_message, sizeof(s_message), "TF 卡不可用，请重新挂载"); return; }
    esp_err_t err = book_comics_scan(s_directory, &s_catalog);
    if (s_directory[0] && (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_ARG || (!s_catalog.count && !s_catalog.incomplete))) {
        s_directory[0] = 0; s_page = s_root_page; copy(s_query, sizeof(s_query), s_root_query);
        book_comics_dispose(&s_catalog); err = book_comics_scan(NULL, &s_catalog);
        copy(s_message, sizeof(s_message), "漫画目录已失效，已返回漫画书架");
    }
    if (s_catalog.incomplete || (err != ESP_OK && err != ESP_ERR_NOT_FOUND))
        copy(s_message, sizeof(s_message), err == ESP_ERR_NO_MEM ? "内存不足，点刷新重试" : "部分目录不可读，点刷新重试");
    clamp(); s_revision = book_store_revision();
}
void app_comics_request_chapters(const char *directory, int page, const char *message) {
    copy(s_requested_directory, sizeof(s_requested_directory), directory ? directory : "");
    copy(s_requested_message, sizeof(s_requested_message), message ? message : "");
    s_requested_page = page < 0 ? 0 : page; s_request = true;
}
bool app_comics_resume_context(char *directory, size_t capacity) {
    if (!directory || strlen(s_directory) >= capacity) return false;
    copy(directory, capacity, s_directory); return true;
}
int app_comics_saved_page(const char *directory) {
    if (!directory) return -1;
    int page = -1;
    nvs_handle_t handle;
    if (nvs_open("comic_nav", NVS_READONLY, &handle) == ESP_OK) {
        char saved[BOOK_STORE_PATH_MAX]; size_t size = sizeof(saved); uint32_t value = 0;
        if (nvs_get_str(handle, "directory", saved, &size) == ESP_OK && !strcmp(saved, directory)) {
            if (nvs_get_u32(handle, "page", &value) == ESP_OK && value < INT32_MAX) page = (int)value;
            if (nvs_get_u32(handle, "root_page", &value) == ESP_OK && value < INT32_MAX) s_root_page = (int)value;
        }
        nvs_close(handle);
    }
    return page;
}
bool app_comics_request_resume(const char *directory) {
    if (!directory || strlen(directory) >= BOOK_STORE_PATH_MAX) return false;
    int page = app_comics_saved_page(directory);
    app_comics_request_chapters(directory, page < 0 ? 0 : page, ""); return true;
}
static bool ready(void) {
    for (int row = 0; row < ui_shelf_rows(s_style); ++row)
        if (index_at(row) != SIZE_MAX && !s_covers[row].ready) return false;
    return true;
}
static bool prepare(bool decode) {
    (void)decode;
    bool was_ready = ready();
    for (int row = 0; row < ui_shelf_rows(s_style); ++row) {
        size_t index = index_at(row);
        if (index == SIZE_MAX || s_covers[row].ready) continue;
        book_comic_entry_t *entry = &s_catalog.entries[index];
        EpdRect image = ui_shelf_image(s_style, row);
        book_cover_job_t request = {.fast = app_settings_main_fast_refresh(),
            .width = image.width, .height = image.height, .stride = UI_SHELF_FAST_STRIDE,
            .contrast = app_settings_system_contrast(),
            .phase = (image.x & 3) | ((image.y & 3) << 2), .x = image.x, .y = image.y};
        copy(request.path, sizeof(request.path), entry->path);
        copy(request.title, sizeof(request.title), entry->name);
        if (!s_cover_generation) s_cover_generation = book_cover_jobs_begin();
        if (!book_cover_jobs_submit(s_cover_generation, row, &request)) continue;
        book_cover_result_t result = {0};
        int state = book_cover_jobs_take(s_cover_generation, row, &result);
        if (!state) continue;
        s_covers[row].ready = true; s_covers[row].index = index;
        s_covers[row].fingerprint = state > 0 ? result.source_fingerprint
            : book_cover_source_fingerprint(entry->path);
        if (state > 0) {
            s_covers[row].bits = result.fast ? result.pixels : NULL;
            s_covers[row].gray = result.fast ? NULL : result.pixels;
        }
    }
    return !was_ready && ready();
}
static void draw_cover(uint8_t *fb, int row) {
    size_t index = index_at(row); if (index == SIZE_MAX) return;
    EpdRect image = ui_shelf_image(s_style, row);
    if (s_pressed == row) image.y -= ui_shelf_lift(s_style);
    epd_fill_rect(image, UI_GRAY_LIGHT, fb);
    if (ready() && (s_covers[row].gray || s_covers[row].bits))
        ui_shelf_cover_pixels(fb, image, s_covers[row].gray, BOOK_COVER_W, BOOK_COVER_H,
                               s_covers[row].bits, app_settings_main_fast_refresh());
    else {
        char title[128]; copy(title, sizeof(title), s_catalog.entries[index].name); fit(title, 26, image.width - 22);
        ui_text_vc(fb, image.x + image.width / 2, image.y + image.height / 2, 26, title, EPD_DRAW_ALIGN_CENTER, false);
    }
    ui_draw_round_rect(fb, image, 0, UI_GRAY_BLACK);
}
static EpdRect field(void) { return (EpdRect){36, 200, 612, 88}; }
static app_redraw_t input_update(app_ctx_t *ctx, bool force) {
    ui_keyboard_update_t update = ui_keyboard_update(ctx->fb, 610, field(), UI_PX_BODY, false, NULL, force);
    if (!update.area.width) return APP_REDRAW_NONE;
    s_area = update.area; s_feedback = true; s_feedback_mode = update.layout ? MODE_GL16 : MODE_DU;
    return APP_REDRAW_AREA;
}
static void draw_header_button(uint8_t *fb, int slot) {
    ui_shelf_header_button(fb, ui_shelf_header_rect(slot),
        slot ? "刷新" : s_query[0] ? "搜索中" : "搜索", s_pressed == 21 + slot);
}
static void draw_pager(uint8_t *fb) {
    int pressed = s_key_pressed ? (s_key_pressed < 0 ? UI_PAGER_PREVIOUS : UI_PAGER_NEXT) : UI_PAGER_NONE;
    if (s_pressed >= 23 && s_pressed <= 26) pressed = s_pressed - 23;
    ui_shelf_pager_state(fb, (int)count(), s_page, pages(), s_directory[0] ? "章" : "部漫画", s_key_mode, pressed);
}
static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx; ui_clear_page(fb); ui_text_set_system_scale(true);
    ui_fill_rect_fast((EpdRect){0, 0, UI_LOCK_WIDTH, UI_NAV_TOP}, UI_GRAY_WHITE, fb);
    ui_nav_status(fb);
    if (s_search) {
        ui_nav_back(fb, 36, 79); ui_text_vc(fb, 342, 107, 34, "搜索当前目录", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_input_draw(fb, &s_input, field(), UI_PX_BODY, false, NULL);
        ui_draw_button(fb, (EpdRect){36,392,180,66}, "清空", false);
        ui_draw_button(fb, (EpdRect){468,392,180,66}, "应用搜索", false);
        ui_keyboard_draw(fb, 610); ui_nav_draw(fb, UI_NAV_COMICS); return;
    }
    if (s_directory[0]) ui_nav_back(fb, 36, 79);
    char title[256]; copy(title, sizeof(title), s_directory[0] ? strrchr(s_directory, '/') + 1 : "漫画");
    fit(title, s_directory[0] ? 30 : 52, s_directory[0] ? 290 : 326);
    ui_text(fb, s_directory[0] ? 112 : 36, 90, s_directory[0] ? 30 : 52, title, EPD_DRAW_ALIGN_LEFT, false);
    draw_header_button(fb, 0);
    draw_header_button(fb, 1);
    if (s_style == 5) {
        ui_fill_round_rect(fb, ui_shelf_list_frame(), 20, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, ui_shelf_list_frame(), 20, 0x50);
    }
    for (int row = 0; row < ui_shelf_rows(s_style); ++row) {
        size_t index = index_at(row); if (index == SIZE_MAX) break;
        book_comic_entry_t *entry = &s_catalog.entries[index]; draw_cover(fb, row);
        char detail[256], progress[288];
        if (!s_directory[0]) {
            snprintf(detail, sizeof(detail), "%u 章", (unsigned)entry->chapters);
            snprintf(progress, sizeof(progress), entry->latest[0] ? "最近：%s" : "尚未阅读%s", entry->latest);
        } else {
            copy(detail, sizeof(detail), "章节进度");
            snprintf(progress, sizeof(progress), entry->has_progress ? "已读 %u%%" : "未开始 · %u%%", entry->pct);
        }
        if (s_style == 5) ui_shelf_list_details(fb, row, entry->name, detail, entry->pct, progress);
        else {
            EpdRect card = ui_shelf_row(s_style, row);
            char name[256]; copy(name, sizeof(name), entry->name); fit(name, 18, card.width - 8);
            ui_text_vc(fb, card.x + card.width / 2, card.y + 243, 18, name, EPD_DRAW_ALIGN_CENTER, false);
            char info[288];
            if (s_directory[0]) copy(info, sizeof(info), progress);
            else snprintf(info, sizeof(info), entry->latest[0] ? "%s · %s" : "%s%s", detail, entry->latest);
            fit(info, 14, card.width - 8);
            ui_text_vc(fb, card.x + card.width / 2, card.y + 257, 14, info, EPD_DRAW_ALIGN_CENTER, false);

        }
    }
    ui_shelf_furniture(fb, s_style, app_settings_main_fast_refresh());
    if (!count()) {
        ui_text_vc(fb, 342, 462, 28, s_scan_pending ? "正在读取 TF 卡" : s_query[0] ? "没有匹配的漫画" : "暂无漫画", EPD_DRAW_ALIGN_CENTER, false);
        ui_text_vc(fb, 342, 512, 20, "TF / comics / 漫画名 / 章节.cbz", EPD_DRAW_ALIGN_CENTER, false);
    }
    if (s_message[0]) ui_text(fb, 36, 991, 17, s_message, EPD_DRAW_ALIGN_LEFT, false);
    draw_pager(fb);
    ui_nav_draw(fb, UI_NAV_COMICS);
}
static bool present(app_ctx_t *ctx, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_NONE || redraw == APP_REDRAW_DONE) return true;
    if (redraw == APP_REDRAW_AREA && s_feedback) {
        s_feedback = false;
        guard_draw_result(ctx->hl, update_display_area_diff_with(ctx->hl,
            s_feedback_mode == MODE_DU ? &E0470_FOLLOW_WAVEFORM : &E0470_WAVEFORM, s_feedback_mode, s_area));
        return true;
    }
    if (!s_search && !s_scan_pending) prepare(false);
    render(ctx, ctx->fb);
    if (s_page_transition) { display_main_transition_shelf_page(); s_page_transition = false; }
    guard_draw_result(ctx->hl, redraw == APP_REDRAW_FULL ? update_display_mode(ctx->hl, MODE_GC16) : update_display_fast_page(ctx->hl));
    return true;
}
static void on_enter(app_ctx_t *ctx) {
    s_style = app_settings_shelf_style(); s_search = false; s_pressed = -1; s_feedback = false;
    s_key_mode = false; s_key_delta = s_key_pressed = 0;
    if (s_request) {
        if (strcmp(s_directory, s_requested_directory)) s_query[0] = 0;
        copy(s_directory, sizeof(s_directory), s_requested_directory); s_page = s_requested_page;
        copy(s_message, sizeof(s_message), s_requested_message); s_request = false;
    } else s_message[0] = 0;
    s_poll = ctx->now_ms; scan();
}
static void comics_on_exit(app_ctx_t *ctx) {
    (void)ctx; ui_keyboard_end(); s_search = false; s_pressed = -1;
    s_key_mode = false; s_key_delta = s_key_pressed = 0;
    drop_covers(); book_comics_dispose(&s_catalog);
}
static app_redraw_t back(app_ctx_t *ctx) {
    if (s_search) { ui_keyboard_end(); s_search = false; return APP_REDRAW_PAGE; }
    if (s_directory[0]) {
        s_directory[0] = 0; s_page = s_root_page; copy(s_query, sizeof(s_query), s_root_query);
        s_message[0] = 0; scan(); return APP_REDRAW_PAGE;
    }
    ui_nav_request(ctx, 0); return APP_REDRAW_NONE;
}
static app_redraw_t turn(int direction) {
    int next = s_page + direction;
    if (next < 0 || next >= pages()) return APP_REDRAW_NONE;
    s_page = next; s_pressed = -1; drop_covers(); s_page_transition = true; return APP_REDRAW_PAGE;
}
static int hit(uint16_t x, uint16_t y) {
    int nav = ui_nav_hit(x, y); if (nav >= 0) return 100 + nav;
    if (s_directory[0] && x < 112 && y < 160) return 20;
    if (ui_rect_hit(ui_shelf_header_rect(0), x, y)) return 21;
    if (ui_rect_hit(ui_shelf_header_rect(1), x, y)) return 22;
    int pager = ui_pager_hit(x, y);
    if (pager >= 0) return 23 + pager;
    for (int row = 0; row < ui_shelf_rows(s_style); ++row)
        if (index_at(row) != SIZE_MAX && ui_rect_hit(ui_shelf_row(s_style, row), x, y)) return row;
    return -1;
}
static app_redraw_t pager_feedback(app_ctx_t *ctx) {
    s_area = ui_pager_area();
    ui_fill_rect_fast(s_area, UI_GRAY_WHITE, ctx->fb); draw_pager(ctx->fb);
    s_feedback_mode = MODE_DU; s_feedback = true;
    return APP_REDRAW_AREA;
}
static app_redraw_t feedback(app_ctx_t *ctx, int row) {
    if (row >= 23 && row <= 26) return pager_feedback(ctx);
    if (row == 21 || row == 22) {
        s_area = ui_shelf_header_rect(row - 21);
        draw_header_button(ctx->fb, row - 21);
        s_feedback_mode = MODE_DU; s_feedback = true;
        return APP_REDRAW_AREA;
    }
    if (row < 0 || row >= ui_shelf_rows(s_style)) return APP_REDRAW_NONE;
    s_area = ui_shelf_feedback(s_style, row);
    // 先重建固定底色再合成亚克力，避免局部反馈重复叠加其他书轨的灰阶。
    // Rebuild the fixed background before acrylic composition so local feedback never re-blends other rails.
    render(ctx, ctx->fb);
    s_feedback_mode = app_settings_main_fast_refresh() ? MODE_DU : MODE_GL16; s_feedback = true;
    return APP_REDRAW_AREA;
}
static void finish_search(void) {
    copy(s_query, sizeof(s_query), s_draft); s_page = 0; drop_covers(); ui_keyboard_end(); s_search = false;
}
static app_redraw_t gesture(app_ctx_t *ctx, const ui_gesture_event_t *ev) {
    if (s_search) {
        if (ev->type == UI_GESTURE_PRESS && ui_keyboard_press(ev->x0, ev->y0, 610, ctx->now_ms)) return input_update(ctx, false);
        if (ev->type == UI_GESTURE_CANCEL || ev->type == UI_GESTURE_MOVE) {
            if (ui_keyboard_release()) return input_update(ctx, false);
            return APP_REDRAW_NONE;
        }
        if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
        ui_keyboard_release();
        if (ev->y < 160 && ev->x < 112) return back(ctx);
        if (ui_text_input_tap(&s_input, field(), UI_PX_BODY, false, NULL, ev->x, ev->y)) return input_update(ctx, true);
        if (ui_rect_hit((EpdRect){36,392,180,66}, ev->x, ev->y)) {
            s_draft[0] = 0; ui_text_edit_init(&s_input, s_draft, sizeof(s_draft)); ui_keyboard_begin(&s_input, false); return input_update(ctx, true);
        }
        if (ui_rect_hit((EpdRect){468,392,180,66}, ev->x, ev->y)) {
            if (!ui_keyboard_pending()) { finish_search(); return APP_REDRAW_PAGE; }
            return input_update(ctx, false);
        }
        ui_keyboard_result_t result = ui_keyboard_tap(ev->x, ev->y, 610, ctx->now_ms);
        if (result == UI_KEYBOARD_DONE && !ui_keyboard_pending()) { finish_search(); return APP_REDRAW_PAGE; }
        return input_update(ctx, false);
    }
    int start = hit(ev->x0, ev->y0), end = hit(ev->x, ev->y);
    if (ev->type == UI_GESTURE_PRESS && ((start >= 0 && start < 9) || (start >= 21 && start <= 26))) {
        s_pressed = start; return feedback(ctx, start);
    }
    if (ev->type == UI_GESTURE_CANCEL || ev->type == UI_GESTURE_MOVE) {
        int old = s_pressed; s_pressed = -1; return feedback(ctx, old);
    }
    if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R) {
        s_pressed = -1; return turn(ev->type == UI_GESTURE_SWIPE_L ? 1 : -1);
    }
    if (ev->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    int old = s_pressed; s_pressed = -1;
    if (old >= 0) { app_redraw_t r = feedback(ctx, old); if (r == APP_REDRAW_AREA) present(ctx, r); }
    if (start != end || start < 0) return APP_REDRAW_NONE;
    if (start >= 100) { ui_nav_request(ctx, start - 100); return APP_REDRAW_NONE; }
    if (start == 20) return back(ctx);
    if (start == 21) {
        copy(s_draft, sizeof(s_draft), s_query); ui_text_edit_init(&s_input, s_draft, sizeof(s_draft));
        ui_keyboard_begin(&s_input, false); s_search = true;
        s_key_mode = false; s_key_delta = s_key_pressed = 0; return APP_REDRAW_PAGE;
    }
    if (start == 22) { s_message[0] = 0; scan(); return APP_REDRAW_PAGE; }
    if (start >= 23 && start <= 26)
        return turn(ui_pager_target(start - 23, s_page, pages()) - s_page);
    size_t index = index_at(start); if (index == SIZE_MAX) return APP_REDRAW_NONE;
    if (!s_directory[0]) {
        s_root_page = s_page; copy(s_root_query, sizeof(s_root_query), s_query); s_query[0] = 0;
        copy(s_directory, sizeof(s_directory), s_catalog.entries[index].path); s_page = 0;
        s_message[0] = 0; scan(); return APP_REDRAW_PAGE;
    }
    if (app_book_request_comic(s_catalog.entries[index].path, s_page)) ui_nav_request(ctx, 1);
    return APP_REDRAW_NONE;
}
static bool main_key(app_ctx_t *ctx, int key, app_redraw_t *redraw) {
    if (s_search) return false;
    if (key == UI_KEY_2) {
        s_key_mode = !s_key_mode; s_key_delta = s_key_pressed = 0;
        *redraw = pager_feedback(ctx); return true;
    }
    if (!s_key_mode || (key != UI_KEY_1 && key != UI_KEY_3)) return false;
    s_key_pressed = key == UI_KEY_1 ? -1 : 1; s_key_delta += s_key_pressed;
    if (s_key_delta > 8) s_key_delta = 8;
    if (s_key_delta < -8) s_key_delta = -8;
    s_key_until = ctx->now_ms + 35; *redraw = pager_feedback(ctx); return true;
}
static app_redraw_t key(app_ctx_t *ctx, int key) {
    s_pressed = -1;
    if (key == UI_KEY_2) { ui_nav_request(ctx, 0); return APP_REDRAW_NONE; }
    return back(ctx);
}
static app_redraw_t tick(app_ctx_t *ctx) {
    if (s_key_mode && s_key_pressed && ctx->now_ms >= s_key_until &&
        !(ctx->touch && ctx->touch->touched)) {
        int direction = s_key_delta; s_key_delta = s_key_pressed = 0;
        app_redraw_t redraw = turn(direction);
        return redraw == APP_REDRAW_NONE ? pager_feedback(ctx) : redraw;
    }
    if (s_search) {
        if (ui_keyboard_idle_tick(ctx->touch && ctx->touch->touched, ctx->now_ms) != UI_KEYBOARD_NONE) return input_update(ctx, false);
        return APP_REDRAW_NONE;
    }
    if (s_scan_pending && ctx->now_ms - s_poll > 500) { s_poll = ctx->now_ms; scan(); return APP_REDRAW_PAGE; }
    if (ctx->touch && ctx->touch->touched) return APP_REDRAW_NONE;
    if (s_style != app_settings_shelf_style()) {
        s_style = app_settings_shelf_style(); clamp(); drop_covers(); return APP_REDRAW_PAGE;
    }
    if (s_revision != book_store_revision()) { scan(); return APP_REDRAW_PAGE; }
    if (ctx->now_ms - s_poll > 2000) {
        s_poll = ctx->now_ms;
        for (int row = 0; row < ui_shelf_rows(s_style); ++row) {
            size_t index = index_at(row);
            if (index != SIZE_MAX && s_covers[row].ready && s_covers[row].fingerprint != book_cover_source_fingerprint(s_catalog.entries[index].path)) {
                drop_covers(); return APP_REDRAW_PAGE;
            }
        }
    }
    if (prepare(true)) return APP_REDRAW_PAGE;
    return APP_REDRAW_NONE;
}
static void media_lost(app_ctx_t *ctx) {
    (void)ctx; ui_keyboard_end(); s_search = false; s_pressed = -1;
    s_key_mode = false; s_key_delta = s_key_pressed = 0;
    drop_covers(); book_comics_dispose(&s_catalog); s_directory[0] = 0; s_query[0] = 0; s_page = 0;
    s_scan_pending = false; copy(s_message, sizeof(s_message), "TF 卡已移除");
}
static void media_ready(app_ctx_t *ctx) { (void)ctx; s_scan_pending = true; }
void app_comics_save_navigation(const char *directory, int page) {
    nvs_handle_t handle;
    if (nvs_open("comic_nav", NVS_READWRITE, &handle) == ESP_OK) {
        if (nvs_set_str(handle, "directory", directory) == ESP_OK &&
            nvs_set_u32(handle, "page", page < 0 ? 0 : (uint32_t)page) == ESP_OK &&
            nvs_set_u32(handle, "root_page", (uint32_t)s_root_page) == ESP_OK) (void)nvs_commit(handle);
        nvs_close(handle);
    }
}
static void before_lock(app_ctx_t *ctx) {
    (void)ctx; s_pressed = -1; ui_keyboard_release(); s_key_delta = s_key_pressed = 0;
    app_comics_save_navigation(s_directory, s_page);
}
static bool edge_back(app_ctx_t *ctx) { (void)ctx; return s_search || s_directory[0]; }
static bool back_swipe_area(app_ctx_t *ctx, const ui_gesture_event_t *event) {
    (void)ctx; return !s_search || event->y0 < 610;
}
static bool main_visible(app_ctx_t *ctx) { (void)ctx; return !s_search; }
static EpdRect area_hint(app_ctx_t *ctx) { (void)ctx; return s_area; }
const app_desc_t app_comics = {
    .title = "漫画 Comics", .detail = "TF 卡漫画与章节", .owns_keys = true,
    .render = render, .present = present, .on_enter = on_enter, .on_exit = comics_on_exit,
    .on_gesture = gesture, .on_key = key, .on_tick = tick,
    .on_media_lost = media_lost, .on_media_ready = media_ready, .on_before_lock = before_lock,
    .main_page_visible = main_visible, .area_hint = area_hint, .on_main_key = main_key,
    .persistent_navigation = true, .edge_back_enabled = edge_back, .back_swipe_area = back_swipe_area,
};
