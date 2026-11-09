/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：图片与缓存主机测试的基础类型；任务由pthread替身实现。
 * English: Basic image/cache host-test types; pthread shims implement tasks.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_heap_caps.h"
#define MALLOC_CAP_DEFAULT 3
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
