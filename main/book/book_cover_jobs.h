/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "book_cover.h"
#define BOOK_COVER_JOB_SLOTS 9
/// 请求只存快照，不借用页面字符串。/ Requests own snapshots instead of borrowing page strings.
typedef struct {
    char path[288],title[256],author[128];
    bool list,fast,natural;
    // Reader idle preparation must never render placeholder glyphs with the reading face.
    bool artwork_only;
    unsigned width,height,stride,contrast,phase;
    int x,y;
} book_cover_job_t;
typedef struct {
    uint8_t *pixels;
    unsigned width,height;
    bool fast,white_edge;
    off_t size;
    int64_t modified;
    /// 预制漫画封面与归档的联合指纹。/ Combined fingerprint of prepared comic covers and archives.
    uint64_t source_fingerprint;
} book_cover_result_t;
/// 新页面代次取消旧结果；切页无需等待解码。/ A new page generation discards stale results without joining a decoder.
unsigned book_cover_jobs_begin(void);
bool book_cover_jobs_submit(unsigned generation,unsigned slot,const book_cover_job_t *request);
/// 0待处理，1完成并转交像素所有权，-1失败；无封面时仅在UI生成字形。
/// 0 pending,1 transfers pixels,-1 failed; missing artwork renders its glyphs only on the UI thread.
int book_cover_jobs_take(unsigned generation,unsigned slot,book_cover_result_t *result);
/// 后台复查已收取槽位的来源：0排队/等待，1返回指纹，-1不可用；不执行UI文件访问。
/// Recheck a consumed slot in the worker: 0 queues/waits, 1 returns its fingerprint, -1 unavailable; no UI file IO.
int book_cover_jobs_recheck(unsigned generation,unsigned slot,uint64_t *fingerprint);
void book_cover_jobs_cancel(unsigned generation);
/// 仅卸载/睡眠前收齐文件句柄，导航不等待。/ Join handles only before unmount/sleep; navigation never waits.
bool book_cover_jobs_quiesce(unsigned timeout_ms);

/// 低优先级单槽壁纸预热；与封面一起取消/汇合，忙时由空闲循环重试。/ One low-priority wallpaper slot, cancelled/joined with covers; idle retries while busy.
bool book_cover_jobs_prefetch_wallpaper(const char *path);
