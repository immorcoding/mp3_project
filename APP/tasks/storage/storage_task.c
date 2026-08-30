/**
  ******************************************************************************
  * @file    storage_task.c
  * @brief   Storage Task 的 FreeRTOS 调度循环。
  *
  * @details
  *          主循环只消费 SD 卡检测通知并执行 30 ms 静默消抖。SDMMC DMA 和
  *          QSPI/MDMA 等待不会出现在该循环中：它们分别在同一 Storage Task
  *          发起的 FatFs 执行器与 storage_flash 协调 Module 的嵌套调用中等待
  *          对应通知并完成收尾。Platform SD 生命周期、FatFs 挂载/卸载和介质
  *          日志仍由 storage_sd 模块集中处理，避免任务循环同时持有 RTOS、
  *          Platform 与文件系统细节。
  ******************************************************************************
  */

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/storage_task_config.h"
#include "APP/tasks/storage/storage_flash.h"
#include "APP/tasks/storage/storage_sd.h"
#include "APP/tasks/storage/benchmark/storage_flash_benchmark.h"
#include "APP/tasks/storage/benchmark/storage_sdram_benchmark.h"
#include "APP/tasks/storage/benchmark/storage_sdram_benchmark_config.h"
#include "APP/app_config.h"

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
  *         主循环只等待索引 0；索引 1 与索引 2 分别由同一任务调用栈中的
  *         Filesystem SD DMA 执行器和 storage_flash 异步 QSPI 协调器临时等待。
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
