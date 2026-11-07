/**
 ******************************************************************************
 * @file    background_jobs.c
 * @brief   后台作业表（见 background_jobs.h）。
 ******************************************************************************
 */

#include "APP/tasks/background/background_jobs.h"
#include "APP/tasks/background/background_flash_pressure.h"
#include "APP/tasks/background/background_task_config.h"

/*
 * 钩子关闭时这一行是 NULL：表里没有作业，函数符号也不被引用，链接器（--gc-sections）把钩子的代码和静态数据丢掉，
 * 默认构建不为它付出代码和 RAM。写成条件表达式而不是 #if，让钩子的源文件在默认构建里照常编译。
 */
static const background_job_step_t background_jobs[] = {
    (BACKGROUND_HOOK_FLASH_PRESSURE != 0U) ? background_flash_pressure_step : NULL,
};

/**
 * @brief  取作业表。
 * @param[out] count 项数，可为 NULL。
 * @return 表首地址（含空项）。
 */
const background_job_step_t *background_jobs_table(size_t *count)
{
    if (count != NULL)
    {
        *count = sizeof(background_jobs) / sizeof(background_jobs[0]);
    }

    return background_jobs;
}

/**
 * @brief  各钩子投启动日志（DIAG-7：没有 HOOK 行就是没开）。
 */
void background_jobs_announce(void)
{
    background_flash_pressure_announce();
}
