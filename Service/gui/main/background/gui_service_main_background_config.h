/**
  ******************************************************************************
  * @file    gui_service_main_background_config.h
  * @brief   Main Screen 局部毛玻璃的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_BACKGROUND_CONFIG_H
#define GUI_SERVICE_MAIN_BACKGROUND_CONFIG_H

#include "Platform/lcd/platform_lcd.h"

/* gui_service_main_background.c */
#define SERVICE_GUI_MAIN_BACKGROUND_WALLPAPER_BLUR_RADIUS  (10U)  /* Music 局部毛玻璃使用的全屏壁纸模糊半径。 */
#define SERVICE_GUI_MAIN_BACKGROUND_TABS_MAX_WIDTH         (PLATFORM_LCD_WIDTH)  /* MusicModeTabs 局部背景最大宽度；用整屏宽度，避免写死 view/ 的相对宽度。 */
#define SERVICE_GUI_MAIN_BACKGROUND_TABS_MAX_HEIGHT        (PLATFORM_LCD_HEIGHT)  /* MusicModeTabs 局部背景最大高度；用整屏高度，避免写死 view/ 的相对高度。 */

#endif /* GUI_SERVICE_MAIN_BACKGROUND_CONFIG_H */
