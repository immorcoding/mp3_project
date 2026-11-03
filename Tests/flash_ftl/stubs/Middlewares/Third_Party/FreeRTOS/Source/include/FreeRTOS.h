/**
 * @file FreeRTOS.h
 * @brief 主机测试的最小 FreeRTOS 头替身：tick 类型、断言与临界区空宏，不模拟真实调度器。
 *
 * @details
 *          主机测试是单线程的。互斥量替身（见 semphr.h）在同一把锁被重复获取时直接中止进程，
 *          因为真实固件里这会自死锁（`configUSE_RECURSIVE_MUTEXES` 为 0）。
 */
#ifndef FILE_BENCH_TEST_FREERTOS_H
#define FILE_BENCH_TEST_FREERTOS_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#define configTICK_RATE_HZ 1000U
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xFFFFFFFFU
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define portYIELD_FROM_ISR(woken) ((void)(woken))
#define configASSERT(condition)                                                       \
    do                                                                                \
    {                                                                                 \
        if (!(condition))                                                             \
        {                                                                             \
            fprintf(stderr, "configASSERT failed: %s:%d\n", __FILE__, __LINE__);      \
            abort();                                                                  \
        }                                                                             \
    } while (0)

/** @brief 互斥量替身的静态存储：只记录是否已被持有。 */
typedef struct
{
    int Taken;
} StaticSemaphore_t;

#endif
