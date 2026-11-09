/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：压缩开机位图的尺寸检查与有界绘制。
 * English: Size checks and bounded rendering for the compressed boot bitmap.
 * 冻结：像素、灰阶与旋转保持原图，不分配整屏解码缓存。
 * Frozen: Preserve source pixels, gray levels and rotation without a full-frame decode buffer.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// 返回KBP1头部声明的原图字节数，无效头部返回0。/ Return the KBP1 declared raw byte count, or zero for an invalid header.
size_t ui_boot_image_raw_size(const uint8_t *encoded, size_t size);
/// 先验证完整长度与CRC，再按当前旋转绘制；失败保持目标帧不变。/ Validate exact length and CRC before drawing in the current rotation; leave the target unchanged on failure.
bool ui_boot_image_draw(uint8_t *framebuffer, const uint8_t *encoded, size_t size);
