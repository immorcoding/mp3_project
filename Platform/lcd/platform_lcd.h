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

#define PLATFORM_LCD_WIDTH   240u /* 当前产品 LCD 可见宽度，单位为像素。 */
#define PLATFORM_LCD_HEIGHT  320u /* 当前产品 LCD 可见高度，单位为像素。 */

/** @brief Platform LCD 对上公开的 24 位显示标识快照。 */
typedef struct
{
    uint8_t ID1; /**< RDDID 的第 1 个数据字节。 */
    uint8_t ID2; /**< RDDID 的第 2 个数据字节。 */
    uint8_t ID3; /**< RDDID 的第 3 个数据字节。 */
} Platform_LCD_IDTypeDef;

/** @brief Platform LCD 异步像素传输的最终事件类型。 */
typedef enum
{
    PLATFORM_LCD_TRANSFER_COMPLETE = 0, /**< 全部像素已移出 SPI MOSI。 */
    PLATFORM_LCD_TRANSFER_ERROR         /**< SPI DMA 或 SPI EOT 发生错误。 */
} Platform_LCD_TransferEventTypeDef;

/** @brief Platform LCD 在中断上下文发布异步像素传输结果的回调类型。 */
typedef void (*Platform_LCD_TransferCallback_t)(
    Platform_LCD_TransferEventTypeDef event,
    void *context);

Platform_StatusTypeDef Platform_LCD_Init(void);
Platform_StatusTypeDef Platform_LCD_ReadID(Platform_LCD_IDTypeDef *id);
Platform_StatusTypeDef Platform_LCD_DrawPixel(uint16_t x,
                                              uint16_t y,
                                              uint16_t color);
Platform_StatusTypeDef Platform_LCD_FillRect(uint16_t x_start,
                                             uint16_t y_start,
                                             uint16_t x_end,
                                             uint16_t y_end,
                                             uint16_t color);
Platform_StatusTypeDef Platform_LCD_FillScreen(uint16_t color);
Platform_StatusTypeDef Platform_LCD_SetTransferCallback(
    Platform_LCD_TransferCallback_t callback,
    void *context);
Platform_StatusTypeDef Platform_LCD_StartWrite(uint16_t x_start,
                                                uint16_t y_start,
                                                uint16_t x_end,
                                                uint16_t y_end,
                                                const uint16_t *pixels);

#endif /* PLATFORM_LCD_H */
