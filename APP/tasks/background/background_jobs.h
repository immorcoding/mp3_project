/**
 ******************************************************************************
 * @file    background_jobs.h
 * @brief   后台作业表：新增一个作业只多一行注册（ARC-22）。
 *
 * @details
 *          现在表里只有 Flash 压力钩子一行，钩子关闭时这一行是空项，整圈没有作业，后台任务一直阻塞。
 *          回收和曲库扫描搬进来时（#220）各加一行。
 ******************************************************************************
 */

#ifndef BACKGROUND_JOBS_H
#define BACKGROUND_JOBS_H

#include <stddef.h>

#include "APP/tasks/background/background_loop.h"

/* 作业表和项数；空项由 `background_loop_run_jobs()` 跳过。 */
const background_job_step_t *background_jobs_table(size_t *count);

/* 后台任务入口调用一次：各个钩子在这里投"我开着"的启动日志（DIAG-7）。 */
void background_jobs_announce(void);

#endif /* BACKGROUND_JOBS_H */
