/**
 ******************************************************************************
 * @file    background_jobs.c
 * @brief   后台作业表（见 background_jobs.h）。
 ******************************************************************************
 */

#include "APP/tasks/background/background_jobs.h"
#include "APP/tasks/background/background_flash_pressure.h"
#include "APP/tasks/background/background_reclaim.h"
#include "APP/tasks/background/background_task_config.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"

/*
 * 内置作业：回收永远在；压力钩子关闭时这一行是 NULL，函数符号也不被引用，链接器（--gc-sections）把钩子的代码和静态数据丢掉，
 * 默认构建不为它付出代码和 RAM。写成条件表达式而不是 #if，让钩子的源文件在默认构建里照常编译。
 */
static const background_job_step_t background_builtin_jobs[] = {
    background_reclaim_step,
    (BACKGROUND_HOOK_FLASH_PRESSURE != 0U) ? background_flash_pressure_step : NULL,
};

/**
 * @brief  拼出作业表：内置作业按序在前，注册表各应用的 `background_step` 按表序在后，空项不占位。
 * @param[in] rows 注册表行；`row_count` 为 0 时可以是 NULL。
 * @param[in] row_count 注册表行数。
 * @param[out] jobs 作业表，放得下 `capacity` 项。
 * @param[in] capacity `jobs` 的项数上限；放不下时 `configASSERT` 停机，不悄悄丢作业。
 * @return 写入 `jobs` 的项数。
 */
size_t background_jobs_build(const AppTable_EntryTypeDef *rows,
                             size_t row_count,
                             background_job_step_t *jobs,
                             size_t capacity)
{
    size_t count = 0U;

    for (size_t index = 0U; index < (sizeof(background_builtin_jobs) / sizeof(background_builtin_jobs[0])); index++)
    {
        if (background_builtin_jobs[index] != NULL)
        {
            configASSERT(count < capacity);
            jobs[count++] = background_builtin_jobs[index];
        }
    }

    for (size_t index = 0U; (rows != NULL) && (index < row_count); index++)
    {
        if (rows[index].background_step != NULL)
        {
            configASSERT(count < capacity);
            jobs[count++] = rows[index].background_step;
        }
    }

    return count;
}

/**
 * @brief  各钩子投启动日志（DIAG-7：没有 HOOK 行就是没开）。
 */
void background_jobs_announce(void)
{
    background_flash_pressure_announce();
}
