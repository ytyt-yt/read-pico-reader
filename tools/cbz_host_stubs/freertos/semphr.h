/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：漫画缓存测试的信号量接口。/ English: Semaphore API for comic-cache tests.
 * 冻结：仅供主机测试。/ Frozen: Host tests only.
 */
#pragma once
#include "FreeRTOS.h"
typedef struct cbz_host_semaphore *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
