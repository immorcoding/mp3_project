/**
 ******************************************************************************
 * @file    foreground_task.h
 * @brief   前台任务：静态创建、入口，以及应用叫醒它的 `foreground_task_wake()`。
 *
 * @details
 *          前台任务轮流调用注册表里各应用的 `foreground_step`，取最小返回值当睡眠时长，任何通知都提前叫醒，
 *          并订阅卷事件（ARC-26）。`foreground_task_wake()` 是 APP 内部接口（#202 M 节授权），不是 Service 接口：
 *          只有 `APP/` 里的任务、应用和 `ui_cell` 的叫醒回调可以调用，任何任务上下文都行，不得从 ISR 调用。
 ******************************************************************************
 */

#ifndef FOREGROUND_TASK_H
#define FOREGROUND_TASK_H

#include "APP/tasks/foreground/foreground_task_config.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/**
 * @brief 前台任务私有的任务通知槽。
 * @note  索引只在本任务的通知数组内有效；其他任务可独立复用数值 0。
 */
typedef enum
{
    FOREGROUND_NOTIFY_EVENT = 0U, /**< 唯一的等待点：叫醒与卷事件以 FLAG 共存。 */
    FOREGROUND_NOTIFY_COUNT       /**< 本任务占用的通知槽数量，不是可等待的事件。 */
} Foreground_NotifyIndexTypeDef;

/* 叫醒前台任务：它马上把所有应用的 step 各调一遍。任务还没跑起来时什么都不做（第一圈本来就调所有应用）。 */
void foreground_task_wake(void);

/* 静态创建前台任务（栈在 AXI 新段 .app_axi_ram）；由 app_tasks.c 的 Boot 任务调用，必须晚于各应用的 `boot_init`。 */
TaskHandle_t foreground_task_create(void);

/* 任务入口，只交给 xTaskCreateStatic。 */
void foreground_task(void *handle);

#endif /* FOREGROUND_TASK_H */
