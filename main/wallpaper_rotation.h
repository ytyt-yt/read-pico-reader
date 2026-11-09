/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0 */
/* 中文：TF壁纸集与锁屏会话；不改变固定壁纸设置。/ English: SD wallpaper sets and lock sessions, independent of the fixed selection. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WALLPAPER_ROTATION_MAX 256u
#define WALLPAPER_ROTATION_DIR "/sdcard/pictures/wallpapers"
/// 刷新单层目录，返回有效候选数；超限通过单独标志报告。/ Scan one directory level; report overflow separately.
unsigned wallpaper_rotation_refresh(void);
bool wallpaper_rotation_overflow(void);
/// 只在新的锁屏入口调用；恢复/PIN不得推进。/ Begin only a new lock; restoration/PIN must never advance.
void wallpaper_rotation_begin(void);
bool wallpaper_rotation_prepare(uint8_t *frame);
/// 仅成功显示后保存图片身份；撤销时保留本次运行跳过的坏图进度。/ Persist displayed identity only on success; retain failed skips in this session on abort.
void wallpaper_rotation_finish(bool displayed);
/// 实际本次图片身份，供锁图校验使用。/ Actual session image identity for lock snapshot validation.
const char *wallpaper_rotation_path(void);
/// 媒体更换丢弃内存状态；空闲预热借用已有可汇合的封面队列。/ Drop state on media changes; idle warming uses the existing joinable cover queue.
void wallpaper_rotation_invalidate(void);
void wallpaper_rotation_idle(void);
