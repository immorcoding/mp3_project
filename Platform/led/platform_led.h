/**
  ******************************************************************************
  * @file    platform_led.h
  * @brief   当前 PCB LED 编号和逻辑亮灭控制的 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_LED_H
#define PLATFORM_LED_H

#include "Platform/platform.h"

/** @brief 当前 PCB 向上公开的 LED 逻辑编号。 */
typedef enum
{
    PLATFORM_LED_ID_STATUS = 0,
    PLATFORM_LED_ID_COUNT
} Platform_LED_IdTypeDef;

/** @brief Platform LED 的逻辑亮灭状态，不表示任何物理高低电平。 */
typedef enum
{
    PLATFORM_LED_OFF = 0,
    PLATFORM_LED_ON
} Platform_LED_OnOffTypeDef;

Platform_StatusTypeDef Platform_LED_Init(void);
Platform_StatusTypeDef Platform_LED_Set(Platform_LED_IdTypeDef id,
                                        Platform_LED_OnOffTypeDef state);
Platform_StatusTypeDef Platform_LED_Toggle(Platform_LED_IdTypeDef id);

#endif /* PLATFORM_LED_H */
