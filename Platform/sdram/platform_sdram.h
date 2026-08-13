/**
  ******************************************************************************
  * @file    platform_sdram.h
  * @brief   当前板载 SDRAM 的 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_SDRAM_H
#define PLATFORM_SDRAM_H

#include <stdint.h>

typedef enum
{
    PLATFORM_SDRAM_OK = 0,
    PLATFORM_SDRAM_NOT_READY,
    PLATFORM_SDRAM_HAL_ERROR,
    PLATFORM_SDRAM_DIAGNOSTIC_FAILED
} Platform_SDRAM_StatusTypeDef;

typedef enum
{
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_NONE = 0,
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_DATA_BUS,
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_ADDRESS_BUS,
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_FULL_PATTERN,
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_CYCLE_COUNTER
} Platform_SDRAM_DiagnosticStageTypeDef;

typedef struct
{
    uint32_t CapacityBytes;
    Platform_SDRAM_DiagnosticStageTypeDef FailedStage;
    uint32_t FailureAddress;
    uint16_t ExpectedValue;
    uint16_t ActualValue;
    uint32_t WriteElapsedMilliseconds;
    uint32_t ReadElapsedMilliseconds;
    uint32_t WriteSpeedMiBPerSecondX100;
    uint32_t ReadSpeedMiBPerSecondX100;
} Platform_SDRAM_DiagnosticsTypeDef;

Platform_SDRAM_StatusTypeDef Platform_SDRAM_Init(void);
Platform_SDRAM_StatusTypeDef Platform_SDRAM_RunDiagnostic(
    Platform_SDRAM_DiagnosticsTypeDef *diagnostics);

#endif /* PLATFORM_SDRAM_H */
