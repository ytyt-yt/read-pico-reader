/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：用pthread运行实际漫画缓存任务，验证完成通知、取消汇合和关闭。
 * English: Run real comic-cache workers with pthreads, validating completion, cancellation and close.
 * 冻结：仅供主机测试，删除仅在任务已汇合后执行。/ Frozen: Host tests only; delete only joined work.
 */
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <pthread.h>
#include <time.h>
#undef free

struct cbz_host_semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    unsigned tokens;
};
struct cbz_host_task {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    unsigned notifications;
    bool stop;
    void (*entry)(void *);
    void *argument;
};
static _Thread_local TaskHandle_t s_self;
int cbz_host_task_fail;
unsigned cbz_host_tasks, cbz_host_semaphores;
static SemaphoreHandle_t semaphore(unsigned tokens) {
    SemaphoreHandle_t result = calloc(1, sizeof(*result));
    assert(result);
    assert(!pthread_mutex_init(&result->mutex, NULL));
    assert(!pthread_cond_init(&result->changed, NULL));
    result->tokens = tokens; ++cbz_host_semaphores;
    return result;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return semaphore(1); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return semaphore(0); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t value, TickType_t wait) {
    assert(value); pthread_mutex_lock(&value->mutex);
    if (!wait && !value->tokens) { pthread_mutex_unlock(&value->mutex); return 0; }
    while (!value->tokens) pthread_cond_wait(&value->changed, &value->mutex);
    --value->tokens; pthread_mutex_unlock(&value->mutex); return 1;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t value) {
    assert(value); pthread_mutex_lock(&value->mutex);
    assert(!value->tokens); ++value->tokens;
    pthread_cond_signal(&value->changed); pthread_mutex_unlock(&value->mutex); return 1;
}
void vSemaphoreDelete(SemaphoreHandle_t value) {
    assert(value); pthread_cond_destroy(&value->changed); pthread_mutex_destroy(&value->mutex);
    --cbz_host_semaphores; free(value);
}
static void *run_task(void *value) {
    s_self = value;
    s_self->entry(s_self->argument);
    return NULL;
}
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *name, unsigned stack,
                                  void *argument, unsigned priority, TaskHandle_t *out, int core) {
    (void)name; (void)stack; (void)priority; (void)core;
    *out = NULL;
    if (cbz_host_task_fail) return 0;
    TaskHandle_t result = calloc(1, sizeof(*result)); assert(result);
    pthread_mutex_init(&result->mutex, NULL); pthread_cond_init(&result->changed, NULL);
    result->entry = entry; result->argument = argument;
    assert(!pthread_create(&result->thread, NULL, run_task, result));
    *out = result; ++cbz_host_tasks;
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t value) {
    assert(value); pthread_mutex_lock(&value->mutex);
    ++value->notifications; pthread_cond_signal(&value->changed); pthread_mutex_unlock(&value->mutex);
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    assert(s_self); pthread_mutex_lock(&s_self->mutex);
    while (!s_self->notifications && !s_self->stop && wait)
        pthread_cond_wait(&s_self->changed, &s_self->mutex);
    bool stop = s_self->stop;
    unsigned notifications = s_self->notifications;
    if (clear) s_self->notifications = 0;
    else if (notifications) --s_self->notifications;
    pthread_mutex_unlock(&s_self->mutex);
    if (stop) pthread_exit(NULL);
    return notifications;
}
void vTaskDelete(TaskHandle_t value) {
    assert(value); pthread_mutex_lock(&value->mutex);
    value->stop = true; pthread_cond_signal(&value->changed); pthread_mutex_unlock(&value->mutex);
    assert(!pthread_join(value->thread, NULL));
    pthread_cond_destroy(&value->changed); pthread_mutex_destroy(&value->mutex);
    --cbz_host_tasks; free(value);
}
void vTaskDelay(TickType_t ticks) {
    struct timespec delay = {.tv_sec = ticks / 1000, .tv_nsec = (ticks % 1000) * 1000000};
    nanosleep(&delay, NULL);
}
