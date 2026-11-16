/**
 ******************************************************************************
 * @file    background_jobs.h
 * @brief   后台作业表：内置作业加注册表各应用的 `background_step`（ARC-22）。
 *
 * @details
 *          表由两部分按序拼成：内置作业（Flash 回收，以及默认关闭的 Flash 压力钩子），然后是注册表里各应用的
 *          `background_step`（空字段跳过）。回收属于后台任务本身，不进注册表；应用的后台作业（现在只有音乐的曲库扫描）
 *          只在注册表多一行，不改这里（“新增应用不改共用层”）。拼表只在任务入口做一次，函数本身没有状态，所以能在主机上测。
 ******************************************************************************
 */

#ifndef BACKGROUND_JOBS_H
#define BACKGROUND_JOBS_H

#include <stddef.h>

#include "APP/apps/app_table.h"
#include "APP/tasks/background/background_loop.h"

/**
 * @brief  拼出作业表。
 * @param[in] rows 注册表行；`row_count` 为 0 时可以是 NULL。
 * @param[in] row_count 注册表行数。
 * @param[out] jobs 作业表，放得下 `capacity` 项。
 * @param[in] capacity `jobs` 的项数上限（`BACKGROUND_JOB_MAX`）；放不下时断言停机，不悄悄丢作业。
 * @return 写入 `jobs` 的项数；全是非空项。
 */
size_t background_jobs_build(const AppTable_EntryTypeDef *rows,
                             size_t row_count,
                             background_job_step_t *jobs,
                             size_t capacity);

/* 后台任务入口调用一次：各个钩子在这里投"我开着"的启动日志（DIAG-7）。 */
void background_jobs_announce(void);

#endif /* BACKGROUND_JOBS_H */
