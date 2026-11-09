/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：漫画缓存测试的任务接口。/ English: Task API for comic-cache tests.
 * 冻结：仅供主机测试。/ Frozen: Host tests only.
 */
#pragma once
#include "FreeRTOS.h"
typedef struct cbz_host_task *TaskHandle_t;
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *name, unsigned stack,
                                  void *argument, unsigned priority, TaskHandle_t *out, int core);
void vTaskDelete(TaskHandle_t task);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
void vTaskDelay(TickType_t ticks);
extern int cbz_host_task_fail;
extern unsigned cbz_host_tasks, cbz_host_semaphores;
