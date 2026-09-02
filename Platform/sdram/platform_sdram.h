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
    PLATFORM_SDRAM_OK = 0,             /**< 本次 SDRAM 操作或诊断成功。 */
    PLATFORM_SDRAM_NOT_READY,          /**< SDRAM 尚未完成初始化。 */
    PLATFORM_SDRAM_HAL_ERROR,          /**< FMC/HAL 初始化或访问失败。 */
    PLATFORM_SDRAM_DIAGNOSTIC_FAILED   /**< 启动诊断发现数据线、地址线或图样错误。 */
} Platform_SDRAM_StatusTypeDef;

typedef enum
{
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_NONE = 0,         /**< 尚未进入诊断阶段。 */
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_DATA_BUS,         /**< 数据线走步测试。 */
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_ADDRESS_BUS,      /**< 地址线走步测试。 */
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_FULL_PATTERN,     /**< 整片图样读写。 */
    PLATFORM_SDRAM_DIAGNOSTIC_STAGE_CYCLE_COUNTER     /**< 吞吐计时所用周期计数器不可用。 */
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
