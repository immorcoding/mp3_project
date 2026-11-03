/**
 * @file filesystem_lock_host.c
 * @brief 主机测试里 filesystem_lock.c 的外部依赖替身：DWT 周期计数器、任务名、tick 与日志。
 *
 * @details
 *          定义 `HOST_LOCK_STUB_NO_LOG_TICK` 的测试自己提供 `xTaskGetTickCount` 与 `Service_Log_Post`。
 *          任务名可由测试经 `host_lock_stub_set_task_name()` 改写，用来模拟 GUI Task 误调文件接口；
 *          周期计数器每读一次前进固定步长，所以持锁时长是可预期的正数。
 */

#include <stdbool.h>
#include <stdint.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/log/log_service.h"

static uint32_t host_lock_cycles;
static const char *host_lock_task_name = "host";
static unsigned host_lock_log_count;

/**
 * @brief 改写 pcTaskGetName 替身返回的任务名。
 * @param[in] name 新任务名，必须在测试期间保持有效。
 */
void host_lock_stub_set_task_name(const char *name)
{
    host_lock_task_name = name;
}

/**
 * @brief 取替身累计收到的日志条数。
 * @return 条数。
 */
unsigned host_lock_stub_log_count(void)
{
    return host_lock_log_count;
}

/**
 * @brief 周期计数器替身：每次读取前进 480 个周期（480 MHz 下 1 µs）。
 * @return 当前读数。
 */
uint32_t CortexM7CycleCounter_Read(void)
{
    host_lock_cycles += 480U;
    return host_lock_cycles;
}

/**
 * @brief 周期计数器替身：固定 480 MHz。
 * @return 频率，单位 Hz。
 */
uint32_t CortexM7CycleCounter_GetFrequencyHz(void)
{
    return 480000000U;
}

/**
 * @brief 当前任务句柄替身：恒为同一个非空句柄。
 * @return 非空句柄。
 */
TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    static int host_task;

    return &host_task;
}

/**
 * @brief 任务名替身。
 * @param[in] task 任务句柄，未使用。
 * @return 测试设置的任务名。
 */
const char *pcTaskGetName(TaskHandle_t task)
{
    (void)task;
    return host_lock_task_name;
}

#ifndef HOST_LOCK_STUB_NO_LOG_TICK
/**
 * @brief tick 替身：恒为 0，持锁时长全部由周期计数器给出。
 * @return 0。
 */
uint32_t xTaskGetTickCount(void)
{
    return 0U;
}

/**
 * @brief 日志替身：只计数。
 * @param[in] level 日志等级，未使用。
 * @param[in] tag 标签，未使用。
 * @param[in] text 正文，未使用。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Log_Post(Service_Log_LevelTypeDef level, const char *tag, const char *text)
{
    (void)level;
    (void)tag;
    (void)text;
    host_lock_log_count++;
    return SERVICE_OK;
}
#endif
