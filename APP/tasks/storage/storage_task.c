/**
 * @file storage_task.c
 * @brief StorageTask 启动、SD 消抖及 Flash 空闲维护调度；传输由 Service 执行。
 */

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/storage_task_config.h"
#include "APP/tasks/storage/storage_flash.h"
#include "APP/tasks/storage/storage_sd.h"
#include "APP/tasks/storage/benchmark/storage_flash_benchmark.h"
#include "APP/tasks/storage/benchmark/storage_flash_benchmark_config.h"
#include "APP/tasks/storage/benchmark/storage_sdram_benchmark.h"
#include "APP/tasks/storage/benchmark/storage_sdram_benchmark_config.h"
#include "APP/app_config.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/**
 * @brief  运行 Storage Task 的存储协调与 SD 卡热插拔调度循环。
 * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
 * @note   每次检测边沿都会通知本任务。任务先取得通知，再以 30 ms 超时等待
 *         下一次通知；只要等待期间仍有新边沿，就重新开始完整静默期。超时后才
 *         调用 storage_sd_process()，因此触点抖动不会触发 SDMMC 或 FatFs 操作。
 *
 *         主循环有界等待索引 0 并提供维护机会；索引 1 与索引 2 由同一任务调用栈中的
 *         Filesystem SD/Flash 私有执行器等待。
 *         它们完成后返回各自调用者，绝不在 IRQ 中提交下一笔传输。
 */
void storage_task(void *handle)
{
    TaskHandle_t task_handle;

    (void)handle;
    task_handle = xTaskGetCurrentTaskHandle();
    storage_sd_init(task_handle);
    storage_flash_init(task_handle);

#if STORAGE_SDRAM_BENCHMARK_ENABLE
    storage_sdram_benchmark_run();
#endif

#if STORAGE_FLASH_BENCHMARK_ENABLE
    storage_flash_benchmark_run();
#endif

    Service_StatusTypeDef flash_status = Service_Filesystem_OpenFlash();
    if (flash_status == SERVICE_OK)
    {
        flash_status = Service_Filesystem_MountFlash();
    }
    if (flash_status == SERVICE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "FLASH", "Filesystem mounted.");
#if STORAGE_FLASH_BENCHMARK_ENABLE && STORAGE_FLASH_BENCHMARK_FILE_ENABLE
        storage_flash_benchmark_run_file();
#endif
    }
    else if (flash_status == SERVICE_NO_FILESYSTEM)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                               "FLASH",
                               "No formatted FTL/FAT volume; explicit format required.");
    }
    else
    {
        (void)Service_Log_Post(
            SERVICE_LOG_LEVEL_ERROR, "FLASH", "Volume unavailable; no automatic format.");
    }

    for (;;)
    {
        uint32_t notified =
            ulTaskNotifyTakeIndexed(FREERTOS_NOTIFY_INDEX_STORAGE_SD_DETECT,
                                    pdTRUE,
                                    pdMS_TO_TICKS(STORAGE_FLASH_MAINTENANCE_PERIOD_MS));
        if (notified)
        {
            while (ulTaskNotifyTakeIndexed(FREERTOS_NOTIFY_INDEX_STORAGE_SD_DETECT,
                                           pdTRUE,
                                           pdMS_TO_TICKS(STORAGE_SD_DEBOUNCE_MS)) != 0U)
            {
                /* 新边沿重新开始静默窗口。传输通知由 Service 独立消费。 */
            }
            storage_sd_process();
        }
        flash_status = Service_Filesystem_MaintainFlash();
        if (flash_status == SERVICE_ERROR || flash_status == SERVICE_TIMEOUT)
        {
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                   "FLASH",
                                   "Maintenance failed; explicit recovery required.");
        }
    }
}
