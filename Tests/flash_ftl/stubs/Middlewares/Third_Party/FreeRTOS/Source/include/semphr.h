/**
 * @file semphr.h
 * @brief 主机测试的互斥量替身：单线程，重复获取同一把锁视为自死锁并中止进程。
 */
#ifndef FILE_BENCH_TEST_SEMPHR_H
#define FILE_BENCH_TEST_SEMPHR_H

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"

typedef StaticSemaphore_t *SemaphoreHandle_t;

/**
 * @brief 创建静态互斥量替身，初始为未持有。
 * @param[out] buffer 互斥量存储。
 * @return 同一个存储的句柄。
 */
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer)
{
    buffer->Taken = 0;
    return buffer;
}

/**
 * @brief 获取互斥量替身；已被持有时中止进程，因为单线程下它永远等不到。
 * @param[in,out] handle 互斥量。
 * @param[in] ticks 超时，未使用。
 * @return 恒为 pdTRUE。
 */
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks)
{
    (void)ticks;
    if (handle->Taken != 0)
    {
        fprintf(stderr, "host mutex taken twice: self-deadlock on target\n");
        abort();
    }
    handle->Taken = 1;
    return pdTRUE;
}

/**
 * @brief 释放互斥量替身；未被持有时中止进程。
 * @param[in,out] handle 互斥量。
 * @return 恒为 pdTRUE。
 */
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t handle)
{
    if (handle->Taken == 0)
    {
        fprintf(stderr, "host mutex given while not taken\n");
        abort();
    }
    handle->Taken = 0;
    return pdTRUE;
}

#endif
