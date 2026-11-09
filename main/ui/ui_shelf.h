/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：文字和漫画书架共用布局、封面像素与点按反馈几何。
 * English: Shared text/comic shelf geometry, cover pixels and press-feedback bounds.
 * 冻结：纯绘制，无文件读取或设置写入。/ Frozen: Paint only, with no file IO or settings writes.
 */
#pragma once
#include "ui_kit.h"

#define UI_SHELF_FAST_STRIDE 21
#define UI_SHELF_FAST_BYTES (UI_SHELF_FAST_STRIDE * 214)
int ui_shelf_rows(uint8_t style);
EpdRect ui_shelf_row(uint8_t style, int row);
EpdRect ui_shelf_image(uint8_t style, int row);
EpdRect ui_shelf_feedback(uint8_t style, int row);
int ui_shelf_lift(uint8_t style);
/// 当前书架的列表外框。/ Current shelf list frame.
EpdRect ui_shelf_list_frame(void);
/// 两个标题按钮的共用命中范围。/ Shared hit bounds for the two header buttons.
EpdRect ui_shelf_header_rect(int slot);
/// 使用书架的圆角、字号和按压反馈绘制标题按钮。/ Paint header buttons with shelf rounding, type size and press feedback.
void ui_shelf_header_button(uint8_t *fb, EpdRect rect, const char *label, bool pressed);
/// 灰阶或已打包像素按同一居中裁切规则呈现。/ Paint gray or packed pixels with the same centered crop.
void ui_shelf_cover_pixels(uint8_t *fb, EpdRect image, const uint8_t *gray, unsigned sw,
                           unsigned sh, const uint8_t *bits, bool fast);
void ui_shelf_furniture(uint8_t *fb, uint8_t style, bool fast);
/// 管理视图可指定原有内容起点。/ Management views may retain their existing content origin.
void ui_shelf_furniture_at(uint8_t *fb, uint8_t style, bool fast, int top);
void ui_shelf_list_title(uint8_t *fb, const char *name, int top);
/// 按调用页的卡片坐标绘制列表详情。/ Draw list details at the caller's card coordinates.
void ui_shelf_list_details_at(uint8_t *fb, EpdRect card, int row, const char *name,
                              const char *description, unsigned progress, const char *label);
void ui_shelf_list_details(uint8_t *fb, int row, const char *name, const char *description,
                           unsigned progress, const char *label);
EpdRect ui_shelf_page_arrow(int direction);
void ui_shelf_pager(uint8_t *fb, int count, int page, int pages, const char *unit);
/// 分页保留main的四向命中、按键模式与局部按压绘制。/ Retain main four-way paging, key mode and local press rendering.
void ui_shelf_pager_state(uint8_t *fb, int count, int page, int pages, const char *unit, bool key_mode, int pressed);
