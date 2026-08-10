/**
  ******************************************************************************
  * @file    storage_task.c
  * @brief   Storage Task 的 FreeRTOS 调度循环。
  *
  * @details
  *          本模块只处理直接任务通知与 30 ms 静默窗口。Platform SD 生命周期、
  *          FatFs 挂载/卸载和介质日志均由 storage_sd 模块集中处理，避免
  *          任务循环同时持有 RTOS、Platform 与文件系统细节。
  ******************************************************************************
  */

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/storage_sd.h"

#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief SD 卡检测输入在最后一个边沿后的最短稳定时间。 */
#define STORAGE_SD_DEBOUNCE_MS 30U

/**
  * @brief  运行 Storage Task 的 SD 卡热插拔调度循环。
  * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
  * @note   每次检测边沿都会通知本任务。任务先取得通知，再以 30 ms 超时等待
  *         下一次通知；只要等待期间仍有新边沿，就重新开始完整静默期。超时后才
  *         调用 storage_sd_process()，因此触点抖动不会触发 SDMMC 或 FatFs 操作。
  */
void storage_task(void *handle)
{
    TaskHandle_t task_handle;

    (void)handle;
    task_handle = xTaskGetCurrentTaskHandle();
    storage_sd_init(task_handle);
    // storage_sd_format_and_mount();

    for (;;)
    {
        (void)ulTaskNotifyTakeIndexed(FREERTOS_NOTIFY_INDEX_STORAGE_SD_DETECT,
                                      pdTRUE,
                                      portMAX_DELAY);

        while (ulTaskNotifyTakeIndexed(FREERTOS_NOTIFY_INDEX_STORAGE_SD_DETECT,
                                       pdTRUE,
                                       pdMS_TO_TICKS(STORAGE_SD_DEBOUNCE_MS)) != 0U)
        {
            /* 每个新边沿都会重新开始完整的静默消抖窗口。 */
        }

        storage_sd_process();
    }
}
