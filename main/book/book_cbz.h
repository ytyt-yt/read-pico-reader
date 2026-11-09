/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：CBZ 漫画的只读页目录、图片与首图封面，不依赖界面。
 * English: Read-only CBZ page index, images and first-page covers without UI dependencies.
 *
 * 冻结：沿用 ZIP 条目和解压上限；自然排序，每图一页；不解包到存储卡。
 * Frozen: Retain ZIP entry/extraction bounds; natural order, one image per page; no extraction to disk.
 */
#pragma once
#include "html_text.h"

typedef struct book_cbz book_cbz_t;
/// 打开 ZIP 并索引非隐藏的 WebP/JPEG/PNG 页；失败清空输出。/ Index visible WebP/JPEG/PNG pages in a ZIP; clear output on failure.
esp_err_t book_cbz_open(const char *path, book_cbz_t **out);
/// 释放文件与目录，允许 NULL。/ Release file and index; accepts NULL.
void book_cbz_close(book_cbz_t *book);
#if KIIKO_BOARD_METALIO47
/// 浅睡前关闭句柄并保留目录；恢复拒绝已改变的归档。/ Suspend handles while retaining the index; reject changed archives on resume.
esp_err_t book_cbz_suspend_io(book_cbz_t *book);
esp_err_t book_cbz_resume_io(book_cbz_t *book, const char *path);
#endif
/// 返回漫画页数。/ Return the comic page count.
size_t book_cbz_count(const book_cbz_t *book);
/// 页标题使用归档内文件名；容量不足返回错误。/ Use the archive filename as page title; fail if capacity is insufficient.
esp_err_t book_cbz_title(const book_cbz_t *book, size_t page, char *out, size_t cap);
/// 生成单图阅读块，调用方 html_text_free。/ Create a one-image reading block; caller uses html_text_free.
esp_err_t book_cbz_load(book_cbz_t *book, size_t page, html_text_t *out);
/// 读取指定页图片并校验 CRC，调用方 free；src 须匹配该页。/ Read and CRC-check the page image; caller frees it; src must match the page.
esp_err_t book_cbz_image(book_cbz_t *book, size_t page, const char *src,
                         uint8_t **data, size_t *size, bool *png);
/// 按页码读取并限制输入预算，供独立预解码句柄使用；调用方 free。
/// Read by page with an input budget for an independent predecode handle; caller frees it.
esp_err_t book_cbz_page_image_bounded(book_cbz_t *book, size_t page,
                                      uint8_t **data, size_t *size, bool *png, size_t budget);
/// 只探测图片头，不代替完整提取的 CRC 校验。/ Probe the image header without replacing full-extraction CRC validation.
esp_err_t book_cbz_dimensions(book_cbz_t *book, size_t page, const char *src,
                              unsigned *width, unsigned *height);
/// 独立读取排序后第一页封面，不改变当前书源；输入受预算约束。/ Read the first sorted page independently of the active source, bounded by budget.
esp_err_t book_cbz_cover_bounded(const char *path, uint8_t **data, size_t *size,
                                 bool *png, size_t budget);
