/**
  ******************************************************************************
  * @file    FreeRTOS.h
  * @brief   模拟器 FreeRTOS 替身：只提供 Service/gui 用到的类型与常量。
  *
  * @details
  *          模拟器单线程运行，GUI Task 即 SDL 主循环。数值与产品
  *          FreeRTOSConfig.h 保持一致（1 kHz Tick、4 个通知槽）。
  ******************************************************************************
  */

#ifndef SIM_FREERTOS_H
#define SIM_FREERTOS_H

#include <assert.h>
#include <stdint.h>

typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#define pdFALSE  ((BaseType_t)0)
#define pdTRUE   ((BaseType_t)1)

#define portMAX_DELAY        ((TickType_t)0xFFFFFFFFUL)
#define portTICK_PERIOD_MS   ((TickType_t)1)
#define portYIELD_FROM_ISR(x) ((void)(x))

#define configTASK_NOTIFICATION_ARRAY_ENTRIES 4

#define configASSERT(x) assert(x) /* 与产品同形：失败即停；`ui_cell` 的写者唯一检查用它。 */

#endif /* SIM_FREERTOS_H */
