/**
  ******************************************************************************
  * @file    storage_sdram_diagnostic.c
  * @brief   Storage Task 上下文中的 SDRAM 启动诊断日志输出。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "APP/tasks/storage/storage_sdram_diagnostic.h"

#include <stdio.h>

#include "Platform/sdram/platform_sdram.h"
#include "Service/log/log_service.h"

/* Private variables ---------------------------------------------------------*/
/** @brief SDRAM 启动诊断日志使用的稳定标签。 */
static const char storage_sdram_log_tag[] = "SDRAM";

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  执行 SDRAM 破坏性诊断并将结果投递到 Log Service。
  * @note   此函数由 Storage Task 在启动早期调用。由于 Storage Task 的优先级高于
  *         Log Task，日志可能在诊断结束并主动阻塞后才显示，这是预期行为。
  */
void storage_sdram_diagnostic_run(void)
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
        (void)LogService_Post(LOG_LEVEL_ERROR, storage_sdram_log_tag, text);
        return;
    }

    (void)snprintf(text,
                   sizeof(text),
                   "Test passed: data bus, address bus, %lu MiB pattern.",
                   (unsigned long)(diagnostics.CapacityBytes / (1024UL * 1024UL)));
    (void)LogService_Post(LOG_LEVEL_INFO, storage_sdram_log_tag, text);

    (void)snprintf(text,
                   sizeof(text),
                   "Bench write: %lu MiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)(diagnostics.CapacityBytes / (1024UL * 1024UL)),
                   (unsigned long)diagnostics.WriteElapsedMilliseconds,
                   (unsigned long)(diagnostics.WriteSpeedMiBPerSecondX100 / 100UL),
                   (unsigned long)(diagnostics.WriteSpeedMiBPerSecondX100 % 100UL));
    (void)LogService_Post(LOG_LEVEL_INFO, storage_sdram_log_tag, text);

    (void)snprintf(text,
                   sizeof(text),
                   "Bench read: %lu MiB, %lu ms, %lu.%02lu MiB/s.",
                   (unsigned long)(diagnostics.CapacityBytes / (1024UL * 1024UL)),
                   (unsigned long)diagnostics.ReadElapsedMilliseconds,
                   (unsigned long)(diagnostics.ReadSpeedMiBPerSecondX100 / 100UL),
                   (unsigned long)(diagnostics.ReadSpeedMiBPerSecondX100 % 100UL));
    (void)LogService_Post(LOG_LEVEL_INFO, storage_sdram_log_tag, text);
}
