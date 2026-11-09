/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：两张有界漫画灰阶缓存与独立归档句柄的空闲预解码，不依赖字体或显示。
 * English: Two bounded comic grayscale caches and idle predecode through an independent archive handle.
 *
 * 冻结：不绘图、不推屏、不使用当前书源句柄；停止须汇合任务后释放归档和缓存。
 * Frozen: No drawing, presentation or active-source handles; join workers before freeing archives/caches.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/// 缓存及调用者之间移动所有权的单页灰阶。/ A page bitmap whose ownership moves between cache and caller.
typedef struct {
    size_t page; ///< 漫画页码 / Comic page index
    unsigned max_width, max_height; ///< 对应阅读区大小 / Matching reading-area size
    unsigned width, height; ///< 实际等比图片大小 / Actual aspect-preserved image size
    uint8_t *gray; ///< 所有权随接口转移，最终 free / Transferred ownership, eventually freed
} book_comic_bitmap_t;

/// 打开独立只读预解码句柄；失败不影响当前阅读。/ Open an independent read-only predecode handle; failure leaves reading usable.
esp_err_t book_comic_cache_open(const char *path);
/// 取消并汇合预解码，释放句柄、任务及两张缓存。/ Cancel/join predecode and release its archive, task and two caches.
void book_comic_cache_close(void);
/// 取消并等待当前任务，保留已完成缓存，扫描与锁屏前调用。/ Cancel/join work while retaining completed caches; call before scanning or locking.
void book_comic_cache_pause(void);
/// 前台串行轮询是否仍在准备，已完成时收齐通知，不取消任务。/ Foreground-only serialized poll that reaps completion without canceling work.
bool book_comic_cache_busy(void);
/// 如目标正在准备则等其完成；其他目标取消并汇合。/ Finish an in-flight matching target; cancel/join other targets.
void book_comic_cache_wait(size_t page, unsigned max_width, unsigned max_height);
/// 命中时交出位图所有权并移除槽位，不等待后台任务。/ Transfer/remove a cached bitmap on a hit without waiting for the worker.
bool book_comic_cache_take(size_t page, unsigned max_width, unsigned max_height,
                           book_comic_bitmap_t *out);
/// 接管上一页位图；不足时释放它，总面积不超过两屏。/ Take ownership of the previous bitmap, freeing it if unavailable; total pixels stay within two screens.
void book_comic_cache_put(book_comic_bitmap_t *bitmap);
/// 空闲时有余量才启动目标页预解码，已准备或失败目标不重复提交。/ Predecode an idle target only with headroom, without repeating ready or failed targets.
void book_comic_cache_prefetch(size_t page, unsigned max_width, unsigned max_height);

#if KIIKO_BOARD_METALIO47
/// 汇合后台后挂起独立归档；恢复失败回到同步阅读。/ Join and suspend the independent archive; failed resume falls back to synchronous reading.
esp_err_t book_comic_cache_suspend_io(void);
void book_comic_cache_resume_io(const char *path);
#endif
