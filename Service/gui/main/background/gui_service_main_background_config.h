/**
  ******************************************************************************
  * @file    gui_service_main_background_config.h
  * @brief   Main Screen 局部毛玻璃的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_BACKGROUND_CONFIG_H
#define GUI_SERVICE_MAIN_BACKGROUND_CONFIG_H

#include "Platform/lcd/platform_lcd.h"

#define SERVICE_GUI_MAIN_BACKGROUND_WALLPAPER_BLUR_RADIUS  (10U) /* Music 局部毛玻璃使用的全屏壁纸模糊半径。 */

#define SERVICE_GUI_MAIN_BACKGROUND_TABS_MAX_WIDTH  (PLATFORM_LCD_WIDTH) /* MusicModeTabs 局部背景允许的最大宽度。 使用整屏宽度，避免将当前 SquareLine 的 90% 相对宽度写死到 Service。 */

#define SERVICE_GUI_MAIN_BACKGROUND_TABS_MAX_HEIGHT  \
    ((PLATFORM_LCD_HEIGHT * 3U) / 5U)

#endif /* GUI_SERVICE_MAIN_BACKGROUND_CONFIG_H */
