/**
  ******************************************************************************
  * @file    gui_service_main_config.h
  * @brief   Main Screen 运行时视觉配置。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_CONFIG_H
#define GUI_SERVICE_MAIN_CONFIG_H

#include "Platform/lcd/platform_lcd.h"

/** @brief Music 局部毛玻璃使用的全屏壁纸模糊半径。 */
#define SERVICE_GUI_MAIN_WALLPAPER_BLUR_RADIUS  (10U)

/**
 * @brief MusicModeTabs 局部背景允许的最大宽度。
 * @note 使用整屏宽度，避免将当前 SquareLine 的 90% 相对宽度写死到 Service。
 */
#define SERVICE_GUI_MAIN_TABS_BACKGROUND_MAX_WIDTH  (PLATFORM_LCD_WIDTH)

/**
 * @brief MusicModeTabs 局部背景允许的最大高度。
 * @note 当前 Tabview 高度约为屏幕高度的 52%，此处留至 60% 的审校余量。若后续
 *       UI 需要更高的玻璃区域，必须先调整本配置并重新核对 SDRAM 峰值。
 */
#define SERVICE_GUI_MAIN_TABS_BACKGROUND_MAX_HEIGHT  \
    ((PLATFORM_LCD_HEIGHT * 3U) / 5U)

#endif /* GUI_SERVICE_MAIN_CONFIG_H */
