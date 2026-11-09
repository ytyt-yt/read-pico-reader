/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：记录测试堆释放，含使用 free 的产品路径。
 * English: Track test heap releases, including production paths using free.
 * 冻结：仅供主机测试。/ Frozen: Host tests only.
 */
#pragma once
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
void cbz_test_free(void *ptr);
#ifdef __cplusplus
}
#endif
#define free cbz_test_free
