/**
 ******************************************************************************
 * @file    foreground_loop.h
 * @brief   前台任务一圈的判断：每圈把注册表里各应用的 `foreground_step` 各调一次，量每个 step 的耗时，取最小返回值当睡眠时长，
 *          任何通知都提前叫醒。
 *
 * @details
 *          step 是单步函数：每次调用做一小步，返回“多少毫秒后再叫我”（0 马上，`FOREGROUND_WAIT_FOREVER` 等通知）。
 *          所有应用共用这一个前台任务，一步太长会拖住别的应用，没有锁或断言能拦住，所以这里量每个 step 的耗时、记下每个应用的
 *          最长值：超过阈值（初值 50 ms）且刷新了最长值时记一行 WARN，只记录不停机（ARC-26）。没超阈值的最长值汇总成一行 INFO，
 *          至多每 10 s 一行，供板级验收读各应用的最长耗时（R5.6）。
 *          判断放在这里而不是 `for(;;)` 里，是为了能在主机上测；任务入口（`foreground_task.c`）只剩创建和 `for(;;)`。
 ******************************************************************************
 */

#ifndef FOREGROUND_LOOP_H
#define FOREGROUND_LOOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "APP/apps/app_table.h"
#include "APP/tasks/foreground/foreground_task.h"

/* 把表里的每个应用的 foreground_step 各调一次（空字段跳过），量耗时，返回最小的返回值；没有任何 step 返回 FOREGROUND_WAIT_FOREVER。 */
uint32_t foreground_loop_run_steps(const AppTable_EntryTypeDef *rows, size_t count);

/* 毫秒换成等待 tick：0 为 0，FOREGROUND_WAIT_FOREVER 为 portMAX_DELAY，其余向上取整（不会把非零等待变成 0）。 */
TickType_t foreground_loop_wait_ticks(uint32_t wait_ms);

/* 一圈：调一遍 step，调一次 after_round（可为 NULL；用于持锁统计等“每圈一次”的汇总，先于等待），再按最小返回值等通知。
 * 被通知叫醒（含调 step 期间已到的通知）返回真，等满超时返回假。 */
bool foreground_loop_step(const AppTable_EntryTypeDef *rows, size_t count, void (*after_round)(void));

/* 第 app_index 个应用从启动起 step 的最长耗时（微秒）；没有这个应用或没有样本为 0。 */
uint32_t foreground_loop_step_max_us(size_t app_index);

/* 把还没记出的最长值汇总成一行 INFO：now_ms 是当前毫秒时刻；距上一行不满一个周期、或没有新刷新时什么都不做。 */
void foreground_loop_report(uint32_t now_ms);

/* 任务入口调用一次，记下自己的句柄，之后 `foreground_task_wake()` 才知道叫谁；传 NULL 取消绑定。 */
void foreground_loop_bind(TaskHandle_t task);

/* 清空最长值记录与汇总节拍（任务入口调用一次；主机测试每个场景调用）。 */
void foreground_loop_reset(void);

#endif /* FOREGROUND_LOOP_H */
