/**
  ******************************************************************************
  * @file    platform_lcd.h
  * @brief   本板 ST7789 LCD 最小诊断 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_LCD_H
#define PLATFORM_LCD_H

#include <stdint.h>

#include "Platform/platform.h"

/** @brief Platform LCD 对上公开的 24 位显示标识快照。 */
typedef struct
{
    uint8_t ID1; /**< RDDID 的第 1 个数据字节。 */
    uint8_t ID2; /**< RDDID 的第 2 个数据字节。 */
    uint8_t ID3; /**< RDDID 的第 3 个数据字节。 */
} Platform_LCD_IDTypeDef;

Platform_StatusTypeDef Platform_LCD_Init(void);
Platform_StatusTypeDef Platform_LCD_ReadID(Platform_LCD_IDTypeDef *id);

#endif /* PLATFORM_LCD_H */
