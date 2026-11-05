/**
 * @file FreeRTOS.h
 * @brief 主机共用 FreeRTOS 桩：类型、常量与时间基准，不模拟真实调度器。
 */
#ifndef HOST_STUB_FREERTOS_H
#define HOST_STUB_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#define configTICK_RATE_HZ 1000U
/* 与产品 FreeRTOSConfig.h 一致：每个任务 4 个通知槽。 */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 4

#define pdFALSE ((BaseType_t)0)
#define pdTRUE ((BaseType_t)1)
#define pdPASS (pdTRUE)
#define pdFAIL (pdFALSE)

#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint64_t)(ms) * (uint64_t)configTICK_RATE_HZ) / 1000U))
/* 主机桩没有中断，让出请求没有意义，只对参数求值以免产生未使用警告。 */
#define portYIELD_FROM_ISR(x) ((void)(x))

#endif
