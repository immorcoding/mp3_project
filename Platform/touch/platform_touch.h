/**
  ******************************************************************************
  * @file    platform_touch.h
  * @brief   当前 PCB 触摸控制器的 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_TOUCH_H
#define PLATFORM_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#include "Platform/platform.h"

/** @brief 当前触摸控制器报告的第一触点原始坐标。 */
typedef struct
{
    bool IsPressed;
    uint16_t X;
    uint16_t Y;
} Platform_Touch_RawPointTypeDef;

Platform_StatusTypeDef Platform_Touch_Init(void);
Platform_StatusTypeDef Platform_Touch_ReadID(uint8_t *chip_id);
Platform_StatusTypeDef Platform_Touch_ReadRawPoint(
    Platform_Touch_RawPointTypeDef *point);

#endif /* PLATFORM_TOUCH_H */
