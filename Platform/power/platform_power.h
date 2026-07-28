/**
  ******************************************************************************
  * @file    platform_power.h
  * @brief   本板电源管理的公共 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_POWER_H
#define PLATFORM_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "Platform/platform.h"

/**
  * @brief Platform Power 最近一次设备状态和通信错误的只读快照。
  * @note  字段使用无符号整数保存底层枚举值，避免向 APP 暴露 AXP2101
  *        Device 类型；仅用于日志和调试，不作为产品策略的持久状态。
  */
typedef struct
{
    uint32_t DeviceState;   /**< AXP2101 Device 当前持续生命周期状态。 */
    uint32_t DeviceError;   /**< 最近一次失败所在的 Device 语义阶段。 */
    uint32_t BusStatus;     /**< 最近一次归一化通信后端结果。 */
    uint8_t FailedRegister; /**< 最近一次读写失败的 AXP2101 寄存器地址。 */
} Platform_Power_DiagnosticsTypeDef;

Platform_StatusTypeDef Platform_Power_Init(void);
Platform_StatusTypeDef Platform_Power_SetAudio(bool enabled);
Platform_StatusTypeDef Platform_Power_SetLCD(bool enabled);
Platform_StatusTypeDef Platform_Power_GetDiagnostics(
    Platform_Power_DiagnosticsTypeDef *diagnostics);

#endif /* PLATFORM_POWER_H */
