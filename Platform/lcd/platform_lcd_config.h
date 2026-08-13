/**
  ******************************************************************************
  * @file    platform_lcd_config.h
  * @brief   当前 PCB 的 Platform LCD 固定参数。
  *
  * @details
  *          本文件保存 LCD 电源稳定时间、同步 SPI 事务超时和背光有效极性。
  *          它只供 Platform LCD 的 Implementation 使用，不得作为上层绘制
  *          Interface 或 ST7789 Device 参数使用。
  ******************************************************************************
  */

#ifndef PLATFORM_LCD_CONFIG_H
#define PLATFORM_LCD_CONFIG_H

#include "main.h"

/** @brief LCD 电源打开后等待 LDO 和屏模块电源稳定的时间，单位为毫秒。 */
#define PLATFORM_LCD_POWER_SETTLE_DELAY_MS     10u
/** @brief 单个 SPI 阻塞命令或 ID 读取事务的最大等待时间，单位为毫秒。 */
#define PLATFORM_LCD_SPI_TIMEOUT_MS            1000u
/** @brief 本板 LCD 背光使能时的 GPIO 电平。 */
#define PLATFORM_LCD_BACKLIGHT_ENABLED_STATE    GPIO_PIN_SET
/** @brief 本板 LCD 背光关闭时的 GPIO 电平。 */
#define PLATFORM_LCD_BACKLIGHT_DISABLED_STATE   GPIO_PIN_RESET

#endif /* PLATFORM_LCD_CONFIG_H */
