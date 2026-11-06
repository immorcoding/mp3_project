/**
 ******************************************************************************
 * @file    background_loop.c
 * @brief   后台任务一圈的判断实现（见 background_loop.h）。
 *
 * @details
 *          只含"调一遍作业、算睡眠时长、等通知、叫醒"，没有 `for(;;)` 和任务创建，所以能在主机上用共用 FreeRTOS 桩测。
 *          通知用 `eSetBits`：连叫几次合并成一次醒；作业运行期间到达的通知留在槽里，等待立刻返回，不会丢。
 ******************************************************************************
 */

#include "APP/tasks/background/background_loop.h"
#include "APP/tasks/background/background_task.h"
#include "APP/tasks/background/background_task_config.h"

/* 后台任务的句柄：任务入口在第一圈之前写一次，之后只读；32 位指针读写原子。绑定之前 wake 是空操作。 */
static volatile TaskHandle_t background_loop_task;

/**
 * @brief  把表里的每个作业各调一次，返回最小的等待毫秒数。
 * @param[in] jobs 作业单步函数表，空项跳过；`count` 为 0 时可以是 NULL。
 * @param[in] count 表项数。
 * @return 最小返回值；没有任何作业被调用时为 `BACKGROUND_WAIT_FOREVER`。
 * @note   不因为某个作业返回 0 就跳过后面的作业：每圈每个作业恰好一次，顺序就是表的顺序。
 */
uint32_t background_loop_run_jobs(const background_job_step_t *jobs, size_t count)
{
    uint32_t smallest = BACKGROUND_WAIT_FOREVER;

    for (size_t index = 0U; (jobs != NULL) && (index < count); index++)
    {
        uint32_t wait_ms;

        if (jobs[index] == NULL)
        {
            continue;
        }

        wait_ms = jobs[index]();
        if (wait_ms < smallest)
        {
            smallest = wait_ms;
        }
    }

    return smallest;
}

/**
 * @brief  毫秒换成等待 tick。
 * @param[in] wait_ms 毫秒；0 马上，`BACKGROUND_WAIT_FOREVER` 一直等通知。
 * @return tick 数。非零毫秒向上取整，不会变成 0；有限的等待不会等于 `portMAX_DELAY`。
 * @note   `INCLUDE_vTaskSuspend` 为 0，`portMAX_DELAY` 在内核里只是约 49.7 天的有限超时，到期后任务多跑一圈，无害。
 */
TickType_t background_loop_wait_ticks(uint32_t wait_ms)
{
    uint64_t ticks;

    if (wait_ms == BACKGROUND_WAIT_FOREVER)
    {
        return portMAX_DELAY;
    }

    ticks = (((uint64_t)wait_ms * (uint64_t)configTICK_RATE_HZ) + 999ULL) / 1000ULL;
    if (ticks >= (uint64_t)portMAX_DELAY)
    {
        return (TickType_t)(portMAX_DELAY - 1U);
    }

    return (TickType_t)ticks;
}

/**
 * @brief  一圈：调一遍作业，再按最小返回值等通知。
 * @param[in] jobs 作业单步函数表。
 * @param[in] count 表项数。
 * @retval true 被通知叫醒（含作业运行期间已到的通知）。
 * @retval false 等满了超时。
 */
bool background_loop_step(const background_job_step_t *jobs, size_t count)
{
    const TickType_t wait_ticks = background_loop_wait_ticks(background_loop_run_jobs(jobs, count));
    uint32_t flags = 0U;

    return xTaskNotifyWaitIndexed((UBaseType_t)BACKGROUND_NOTIFY_EVENT,
                                  0U,
                                  BACKGROUND_NOTIFY_FLAG_EVENT_MASK,
                                  &flags,
                                  wait_ticks) == pdTRUE;
}

/**
 * @brief  记下后台任务的句柄。
 * @param[in] task 后台任务句柄；传 NULL 取消绑定。
 */
void background_loop_bind(TaskHandle_t task)
{
    background_loop_task = task;
}

/**
 * @brief  叫醒后台任务，让它马上把所有作业各调一遍。
 * @note   任务上下文调用，不得从 ISR 调用。任务入口还没绑定句柄时什么都不做：任务第一圈本来就调所有作业。
 */
void background_task_wake(void)
{
    const TaskHandle_t task = background_loop_task;

    if (task != NULL)
    {
        (void)xTaskNotifyIndexed(task,
                                 (UBaseType_t)BACKGROUND_NOTIFY_EVENT,
                                 BACKGROUND_NOTIFY_FLAG_WAKE,
                                 eSetBits);
    }
}
