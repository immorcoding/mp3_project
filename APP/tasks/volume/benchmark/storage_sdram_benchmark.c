/**
  ******************************************************************************
  * @file    storage_sdram_benchmark.c
  * @brief   SDRAM 诊断（Boot 任务上下文）与基准日志输出。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "APP/tasks/volume/benchmark/storage_sdram_benchmark.h"

#include <stdio.h>

#include "Platform/sdram/platform_sdram.h"
#include "Service/log/log_service.h"

/* Private variables ---------------------------------------------------------*/
/** @brief SDRAM 基准日志使用的稳定标签。 */
static const char storage_sdram_benchmark_log_tag[] = "SDRAM";

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  执行 SDRAM 破坏性诊断与吞吐基准，并将结果投递到 Log Service。
  * @note   此函数由 Boot 任务（`create_task`）在创建 Volume、Storage 等任务之前调用，所以破坏性自检仍然独占 SDRAM
  *         （HWD-2）。Boot 任务的优先级最低，Log Task 在每行日志投递后马上抢占并转交，日志随诊断进度显示。
  *         Boot 栈只有 128 word，本函数加 snprintf 的栈深度核算见 `APP/tasks/volume/README.md`。
  */
void storage_sdram_benchmark_run(void)
{
    Platform_SDRAM_DiagnosticsTypeDef diagnostics;
    const Platform_SDRAM_StatusTypeDef status =
        Platform_SDRAM_RunDiagnostic(&diagnostics);
    char text[128];

    if (status != PLATFORM_SDRAM_OK)
    {
        (void)snprintf(text,
                       sizeof(text),
                       "Test failed: status=%lu, stage=%lu, addr=0x%08lX, expected=0x%04X, actual=0x%04X.",
                       (unsigned long)status,
                       (unsigned long)diagnostics.FailedStage,
                       (unsigned long)diagnostics.FailureAddress,
                       (unsigned int)diagnostics.ExpectedValue,
                       (unsigned int)diagnostics.ActualValue);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_sdram_benchmark_log_tag,
                               text);
        return;
    }

    (void)snprintf(text,
                   sizeof(text),
                   "Test passed: data bus, address bus, %lu MiB pattern.",
                   (unsigned long)(diagnostics.CapacityBytes / (1024UL * 1024UL)));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           storage_sdram_benchmark_log_tag,
                           text);

    (void)snprintf(text,
                   sizeof(text),
                   "Bench write: %lu MiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)(diagnostics.CapacityBytes / (1024UL * 1024UL)),
                   (unsigned long)diagnostics.WriteElapsedMilliseconds,
                   (unsigned long)(diagnostics.WriteSpeedMiBPerSecondX100 / 100UL),
                   (unsigned long)(diagnostics.WriteSpeedMiBPerSecondX100 % 100UL));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           storage_sdram_benchmark_log_tag,
                           text);

    (void)snprintf(text,
                   sizeof(text),
                   "Bench read: %lu MiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)(diagnostics.CapacityBytes / (1024UL * 1024UL)),
                   (unsigned long)diagnostics.ReadElapsedMilliseconds,
                   (unsigned long)(diagnostics.ReadSpeedMiBPerSecondX100 / 100UL),
                   (unsigned long)(diagnostics.ReadSpeedMiBPerSecondX100 % 100UL));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                           storage_sdram_benchmark_log_tag,
                           text);
}
