/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：漫画测试的有界 PSRAM 与故障注入接口。
 * English: Bounded PSRAM and allocation fault injection for comic tests.
 * 冻结：仅供主机测试。/ Frozen: Host tests only.
 */
#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t size, int caps);
void *heap_caps_calloc(size_t count, size_t size, int caps);
void *heap_caps_realloc(void *ptr, size_t size, int caps);
void heap_caps_free(void *ptr);
size_t heap_caps_get_free_size(int caps);
size_t heap_caps_get_largest_free_block(int caps);
