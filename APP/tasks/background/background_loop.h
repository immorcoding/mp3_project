/**
 ******************************************************************************
 * @file    background_loop.h
 * @brief   后台任务一圈的判断：每圈把所有作业各调一次，取最小返回值当睡眠时长，任何通知都提前叫醒。
 *
 * @details
 *          作业是单步函数：每次调用做一小步，返回"多少毫秒后再叫我"（0 马上，`BACKGROUND_WAIT_FOREVER` 等通知）。
 *          这里没有队列、取消接口和作业优先级（ARC-22）：提交作业就是应用改自己的状态再调
 *          `background_task_wake()`。判断放在这里而不是 `for(;;)` 里，是为了能在主机上测。
 *          任务入口（`background_task.c`）只剩创建和 `for(;;) background_loop_step()`。
 ******************************************************************************
 */

#ifndef BACKGROUND_LOOP_H
#define BACKGROUND_LOOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 后台任务私有的任务通知槽；索引只在本任务的通知数组内有效。 */
typedef enum
{
    BACKGROUND_NOTIFY_EVENT = 0U, /**< `background_task_wake()` 置位。 */
    BACKGROUND_NOTIFY_COUNT       /**< 本任务占用的通知槽数量，不是可等待的事件。 */
} Background_NotifyIndexTypeDef;

/**
 * @brief  一个后台作业的单步函数。
 * @return 多少毫秒后再调它：0 马上；`BACKGROUND_WAIT_FOREVER` 等通知（不需要定时）。
 */
typedef uint32_t (*background_job_step_t)(void);

/* 把表里的每个作业各调一次（空项跳过），返回最小的返回值；没有作业返回 BACKGROUND_WAIT_FOREVER。 */
uint32_t background_loop_run_jobs(const background_job_step_t *jobs, size_t count);

/* 毫秒换成等待 tick：0 为 0，BACKGROUND_WAIT_FOREVER 为 portMAX_DELAY，其余向上取整（不会把非零等待变成 0）。 */
TickType_t background_loop_wait_ticks(uint32_t wait_ms);

/* 一圈：调一遍作业，再按最小返回值等通知。被通知叫醒（含调作业期间已到的通知）返回真，等满超时返回假。 */
bool background_loop_step(const background_job_step_t *jobs, size_t count);

/* 任务入口调用一次，记下自己的句柄，之后 `background_task_wake()` 才知道叫谁。 */
void background_loop_bind(TaskHandle_t task);

#endif /* BACKGROUND_LOOP_H */
