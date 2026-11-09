/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：按面板原生行写入漫画灰阶，保留现有抖动相位与屏幕旋转。
 * English: Write comic gray by native panel row, preserving existing dithering and rotation.
 *
 * 冻结：只绘制指定图片，不访问文件、不推屏、不改变阅读区或刷新模式。
 * Frozen: Paint only the specified image; no files, presentation, reading-area or refresh changes.
 */
#pragma once
#include <stdint.h>
/// 灰阶为借用的一字节平面，输出裁剪到屏幕并保留边缘的另一个半字节。
/// Borrow an eight-bit gray plane, clip to the panel and preserve adjacent edge nibbles.
void ui_comic_image_paint(uint8_t *framebuffer, const uint8_t *gray,
                           int width, int height, int left, int top);
