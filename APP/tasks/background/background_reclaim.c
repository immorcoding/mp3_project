/**
 ******************************************************************************
 * @file    background_reclaim.c
 * @brief   Flash 回收作业实现（见 background_reclaim.h）。
 ******************************************************************************
 */

#include "APP/tasks/background/background_reclaim.h"
#include "APP/tasks/background/background_task_config.h"
#include "APP/tasks/volume/volume_event.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

/** @brief 回收作业的状态；只在后台任务里访问。 */
typedef struct
{
    uint32_t SeenSequence; /**< 最近处理过的 Flash 卷序号；变了就从头开始。 */
    TickType_t LastTick;   /**< 上一次回收调用开始的时刻（第一次看到新卷序号时记为那一刻，所以第一次机会在一个周期之后）。 */
    bool Stopped;          /**< 回收失败过一次：停到 Flash 卷序号变化为止。 */
} Background_ReclaimStateTypeDef;

static Background_ReclaimStateTypeDef background_reclaim_state;

/**
 * @brief  毫秒向上取整换成 tick 的反向：tick 数换成毫秒（1 kHz 时相等，其他节拍向上取整，不会把非零变成 0）。
 * @param[in] ticks tick 数。
 * @return 毫秒数。
 */
static uint32_t background_reclaim_ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)((((uint64_t)ticks * 1000ULL) + (uint64_t)configTICK_RATE_HZ - 1ULL) / (uint64_t)configTICK_RATE_HZ);
}

/**
 * @brief  记失败那一条 ERROR（只记一条，之后作业停下，不再重试）。
 * @param[in] status `ReclaimFlash` 返回的失败状态。
 * @note   正文带状态数值（`%d`），96 B 的行缓冲放得下（Service Log 单条正文最多 127 字符）；栈用量很小。
 */
static void background_reclaim_log_failure(Service_StatusTypeDef status)
{
    char text[96];

    (void)snprintf(text,
                   sizeof(text),
                   "Reclaim failed status=%d, stopped until Flash volume sequence changes",
                   (int)status);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, "FLASH", text);
}

/**
 * @brief  回收作业单步（见 background_reclaim.h）：Flash 卷序号为 0 或卷没挂载直接返回；到点给 FTL 一次回收机会；失败一次就停。
 * @return 多少毫秒后再叫它：距下一次回收机会还剩的时间；卷序号为 0、卷没挂载、已因失败停下时为 `BACKGROUND_WAIT_FOREVER`。
 */
uint32_t background_reclaim_step(void)
{
    const Volume_StatusTypeDef flash = volume_status(VOLUME_ID_FLASH);
    const TickType_t period_ticks = pdMS_TO_TICKS(BACKGROUND_RECLAIM_PERIOD_MS);
    Service_StatusTypeDef status;
    TickType_t now;
    TickType_t elapsed;

    if (flash.Sequence == 0U)
    {
        return BACKGROUND_WAIT_FOREVER;
    }

    now = xTaskGetTickCount();
    if (flash.Sequence != background_reclaim_state.SeenSequence)
    {
        background_reclaim_state.SeenSequence = flash.Sequence;
        background_reclaim_state.LastTick = now;
        background_reclaim_state.Stopped = false;
    }

    if (background_reclaim_state.Stopped || !flash.Mounted)
    {
        return BACKGROUND_WAIT_FOREVER;
    }

    elapsed = now - background_reclaim_state.LastTick;
    if (elapsed < period_ticks)
    {
        return background_reclaim_ticks_to_ms(period_ticks - elapsed);
    }

    background_reclaim_state.LastTick = now;
    status = Service_Filesystem_ReclaimFlash();
    if ((status == SERVICE_ERROR) || (status == SERVICE_TIMEOUT))
    {
        background_reclaim_state.Stopped = true;
        background_reclaim_log_failure(status);
        return BACKGROUND_WAIT_FOREVER;
    }

    return BACKGROUND_RECLAIM_PERIOD_MS;
}
