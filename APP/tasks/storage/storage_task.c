/**
 * @file storage_task.c
 * @brief Storage Task 入口：启动 SD/Flash 编排并调度热插拔消抖与空闲回收。
 */

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/storage_task_config.h"
#include "APP/tasks/storage/flash/storage_flash.h"
#include "APP/tasks/storage/sd/storage_sd.h"
#include "APP/tasks/storage/benchmark/storage_sdram_benchmark.h"
#include "APP/tasks/storage/benchmark/storage_sdram_benchmark_config.h"

#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "Service/log/log_service.h"
#include "catalog/storage_catalog.h"

/**
 * @brief  运行 Storage Task 的存储协调与 SD 卡热插拔调度循环。
 * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
 * @note   SDRAM 破坏性自检必须早于 Flash 挂载使用的 FTL 表。随后 Flash 完成
 *         执行器绑定、可选物理基准和挂载策略，再初始化 SD 热插拔。
 *
 *         每次检测边沿都会通知本任务。任务先取得通知，再以 30 ms 超时等待
 *         下一次通知；只要等待期间仍有新边沿，就重新开始完整静默期。超时后才
 *         调用 storage_sd_process()，因此触点抖动不会触发 SDMMC 或 FatFs 操作。
 *
 *         主循环有界等待 STORAGE_NOTIFY_SD_DETECT 并提供回收机会；
 *         STORAGE_NOTIFY_SD_TRANSFER 与 STORAGE_NOTIFY_FLASH_OPERATION 由同一任务
 *         调用栈中的 Filesystem SD/Flash 私有执行器等待。
 *         它们完成后返回各自调用者，绝不在 IRQ 中提交下一笔传输。
 */
void storage_task(void *handle)
{
    TaskHandle_t task_handle;

    (void)handle;
    task_handle = xTaskGetCurrentTaskHandle();

    _Static_assert((unsigned)STORAGE_NOTIFY_COUNT <=
                       (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
                   "Storage Task notify slots exceed FreeRTOS array length");

#if STORAGE_SDRAM_BENCHMARK_ENABLE
    storage_sdram_benchmark_run();
#endif

    if (storage_flash_init(task_handle) != STORAGE_OK)
    {
        /* Handle flash initialization error */
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "STORAGE",
                               "Flash initialization failed.");
    }
    if (storage_sd_init(task_handle) != STORAGE_OK)
    {
        /* Handle SD initialization error */
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "STORAGE",
                               "SD initialization failed.");
    }

    for (;;)
    {
        uint32_t notified = ulTaskNotifyTakeIndexed(STORAGE_NOTIFY_SD_DETECT,
                                                    pdTRUE,
                                                    pdMS_TO_TICKS(STORAGE_FLASH_RECLAIM_PERIOD_MS));
        if (notified)
        {
            while (ulTaskNotifyTakeIndexed(STORAGE_NOTIFY_SD_DETECT,
                                           pdTRUE,
                                           pdMS_TO_TICKS(STORAGE_SD_DEBOUNCE_MS)) != 0U)
            {
                /* 新边沿重新开始静默窗口。传输通知由 Service 独立消费。 */
            }
            if (storage_sd_process() != STORAGE_OK)
            {
                (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                       "STORAGE",
                                       "SD process failed.");
            }
        }
        if (storage_flash_reclaim() != STORAGE_OK)
        {
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                   "STORAGE",
                                   "Flash reclaim failed.");
        }
    }
}
