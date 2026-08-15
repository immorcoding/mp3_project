/**
  ******************************************************************************
  * @file    platform_touch.h
  * @brief   当前 PCB 触摸控制器的 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_TOUCH_H
#define PLATFORM_TOUCH_H

#include <stdint.h>

#include "Platform/platform.h"

Platform_StatusTypeDef Platform_Touch_Init(void);
Platform_StatusTypeDef Platform_Touch_ReadID(uint8_t *chip_id);

#endif /* PLATFORM_TOUCH_H */
