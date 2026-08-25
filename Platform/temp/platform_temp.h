/**
  ******************************************************************************
  * @file    platform_temp.h
  * @brief   当前产品 MCU 结温的 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_TEMP_H
#define PLATFORM_TEMP_H

#include <stdint.h>

#include "Platform/platform.h"

Platform_StatusTypeDef Platform_Temp_Init(void);
Platform_StatusTypeDef Platform_Temp_Read(int32_t *temperature_mC);

#endif /* PLATFORM_TEMP_H */
