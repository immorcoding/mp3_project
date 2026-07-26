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

typedef struct
{
    uint32_t DeviceState;
    uint32_t DeviceError;
    uint32_t BusStatus;
    uint8_t FailedRegister;
} Platform_Power_DiagnosticsTypeDef;

Platform_StatusTypeDef Platform_Power_Init(void);
Platform_StatusTypeDef Platform_Power_SetAudio(bool enabled);
Platform_StatusTypeDef Platform_Power_SetLCD(bool enabled);
Platform_StatusTypeDef Platform_Power_GetDiagnostics(
    Platform_Power_DiagnosticsTypeDef *diagnostics);

#endif /* PLATFORM_POWER_H */
