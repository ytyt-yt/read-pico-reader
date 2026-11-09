/* SPDX-License-Identifier: Apache-2.0
 * 中文：轻量底栏边界，绘制和刷新共用，防止局部扫描碰到固定图标。
 * English: Lightweight navigation bounds shared by painting and presentation to protect fixed icons.
 */
#pragma once
#define UI_NAV_COUNT 5
#define UI_NAV_COMICS 5
// 显示位置与持久页面编号分离；0..3与旧深睡记录一致，4仍为图片页。
// Display positions are separate from persistent page IDs; 0..3 retain old resume IDs and 4 stays Image.
static inline int ui_nav_page_at(int slot) {
    const int pages[UI_NAV_COUNT] = {0, 1, UI_NAV_COMICS, 2, 3};
    return slot >= 0 && slot < UI_NAV_COUNT ? pages[slot] : -1;
}
static inline int ui_nav_slot_for(int page) {
    for (int i = 0; i < UI_NAV_COUNT; ++i) if (ui_nav_page_at(i) == page) return i;
    return -1;
}
static inline int ui_nav_page_valid(int page) { return ui_nav_slot_for(page) >= 0; }
/// 按绘制的整数分界定位显示栏位，覆盖宽度不能整除的边界。/ Locate slots using painted integer boundaries, including uneven widths.
static inline int ui_nav_slot_at(int x, int width) {
    if (width <= 0 || x < 0 || x >= width) return -1;
    for (int slot = 0; slot < UI_NAV_COUNT; ++slot)
        if (x < (slot + 1) * width / UI_NAV_COUNT) return slot;
    return -1;
}
#define UI_NAV_TOP 1096
// 只扩大触控热区，不移动图标或改变内容/底栏的刷新边界。
// Expand touch admission only; icons and content/navigation refresh boundaries stay fixed.
#define UI_NAV_TOUCH_TOP (UI_NAV_TOP - 16)
#define UI_NAV_MARKER_TOP (UI_NAV_TOP + 1)
#define UI_NAV_MARKER_BOTTOM (UI_NAV_TOP + 6)
// 内容向内收至32列边界；横条单独短刷或并入快刷，外扩范围均止于图标首笔之前。
// Round content inward to 32 columns; marker-only or fused fast scans end before any icon ink.
#define UI_NAV_REFRESH_END (UI_NAV_TOP & ~31)
#define UI_NAV_MARKER_SCAN_END ((UI_NAV_MARKER_BOTTOM + 31) & ~31)
