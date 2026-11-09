/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：WebP 单帧缩放灰阶解码，不访问存储或显示硬件。
 * English: Scaled single-frame WebP grayscale decoding without storage or display access.
 * 冻结：拒绝动画；目标像素最多一屏，透明像素合成白底。
 * Frozen: Reject animation; output is bounded to one screen; composite transparency onto white.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/// 与 JPEG/PNG 共用居中裁剪契约；漫画调用方先算等比尺寸。/ Match the JPEG/PNG centered-crop contract; comic callers first compute aspect-fit dimensions.
bool book_webp_grayscale(const uint8_t *data, size_t size,
                          unsigned width, unsigned height, uint8_t *out);
/// 返回 true 时取消下一段解码，失败输出不得呈现。/ Return true to cancel the next decode segment; never present failed output.
typedef bool (*book_image_cancel_fn)(void *context);
/// 预解码可在分段之间取消；NULL 保持普通同步解码。/ Cancel predecode between segments; NULL retains normal synchronous decoding.
bool book_webp_grayscale_cancelable(const uint8_t *data, size_t size,
                                     unsigned width, unsigned height, uint8_t *out,
                                     book_image_cancel_fn cancel, void *context);
