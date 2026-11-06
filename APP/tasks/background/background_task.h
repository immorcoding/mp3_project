/**
 ******************************************************************************
 * @file    background_task.h
 * @brief   后台任务：静态创建、入口，以及应用叫醒它的 `background_task_wake()`。
 *
 * @details
 *          `background_task_wake()` 是 APP 内部接口（#202 M 节授权），不是 Service 接口；
 *          只有 `APP/` 里的任务和应用可以调用，任何任务上下文都行，不得从 ISR 调用。
 ******************************************************************************
 */

#ifndef BACKGROUND_TASK_H
#define BACKGROUND_TASK_H

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/* 叫醒后台任务：它马上把所有作业各调一遍。任务还没跑起来时什么都不做（第一圈本来就调所有作业）。 */
void background_task_wake(void);

/* 静态创建后台任务（栈在 AXI 新段 .app_axi_ram）；由 app_tasks.c 的 Boot 任务调用。 */
TaskHandle_t background_task_create(void);

/* 任务入口，只交给 xTaskCreateStatic。 */
void background_task(void *handle);

#endif /* BACKGROUND_TASK_H */
