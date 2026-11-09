/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：独立漫画目录、自然章节顺序与封面来源，不包含阅读器或界面。
 * English: Independent comic catalog, natural chapter order and cover sources without reader or UI.
 * 冻结：固定根目录，两层扫描，隐藏项与更深目录不收录；逐CBZ路径保存进度。
 * Frozen: Fixed root, two-level scans, no hidden or nested entries; progress stays per CBZ path.
 */
#pragma once
#include "book_store.h"

#ifndef BOOK_COMICS_ROOT
#define BOOK_COMICS_ROOT "/sdcard/comics"
#endif

typedef struct {
    /// 文件夹名或去扩展名的章节名。/ Folder name or extension-free chapter name.
    char name[256];
    /// 漫画目录或章节的完整路径。/ Full comic directory or chapter path.
    char path[BOOK_STORE_PATH_MAX];
    /// 最近阅读章节名。/ Most recently read chapter name.
    char latest[256];
    /// 章节数、文件大小和持久阅读序号。/ Chapter count, file size and persistent reading order.
    uint32_t chapters, size, recent;
    /// 逐章有效进度。/ Valid per-chapter progress.
    uint8_t pct;
    bool has_progress;
} book_comic_entry_t;

typedef struct {
    /// PSRAM中的条目数组，由dispose释放。/ PSRAM entries released by dispose.
    book_comic_entry_t *entries;
    size_t count;
    /// 不完整扫描不得解释为空目录。/ An incomplete scan must not be treated as an empty directory.
    bool incomplete;
} book_comic_catalog_t;

/// directory为空扫描漫画层，否则扫描指定漫画的直接章节。/ Empty directory scans series; otherwise scan direct chapters of one series.
esp_err_t book_comics_scan(const char *directory, book_comic_catalog_t *out);
void book_comics_dispose(book_comic_catalog_t *catalog);
/// 仅接受固定根下一个可见目录中的直接CBZ；不依赖文件存在。/ Accept only a direct visible CBZ in one visible series under the fixed root, without file IO.
bool book_comics_context(const char *chapter, char directory[BOOK_STORE_PATH_MAX]);
/// 无固定长度数值转换，数字段按自然顺序比较。/ Compare digit runs naturally without fixed-width integer conversion.
int book_comics_natural_compare(const char *a, const char *b);
/// 封面来源：首选、次选预制PNG及回退CBZ；不读取归档。/ Resolve primary/secondary PNG and fallback CBZ without reading archives.
bool book_comics_cover_sources(const char *source, char primary[BOOK_STORE_PATH_MAX],
                               char secondary[BOOK_STORE_PATH_MAX], char cbz[BOOK_STORE_PATH_MAX]);
