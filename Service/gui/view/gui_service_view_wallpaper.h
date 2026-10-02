/**
  ******************************************************************************
  * @file    gui_service_view_wallpaper.h
  * @brief   默认系统壁纸：尺寸、像素区与图片描述符。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_VIEW_WALLPAPER_H
#define GUI_SERVICE_VIEW_WALLPAPER_H

#include <stdint.h>

#include "lvgl.h"

#define SERVICE_GUI_VIEW_WALLPAPER_WIDTH   (240U)  /* 全屏壁纸宽度，像素。 */
#define SERVICE_GUI_VIEW_WALLPAPER_HEIGHT  (320U)  /* 全屏壁纸高度，像素。 */
#define SERVICE_GUI_VIEW_WALLPAPER_BYTES   \
    (SERVICE_GUI_VIEW_WALLPAPER_WIDTH * SERVICE_GUI_VIEW_WALLPAPER_HEIGHT * LV_IMG_PX_SIZE_ALPHA_BYTE)

/** @brief 壁纸像素（TRUE_COLOR_ALPHA，RGB565 + A8）；由资源安装写入。 */
extern uint8_t service_gui_view_wallpaper_pixels[SERVICE_GUI_VIEW_WALLPAPER_BYTES];

extern const lv_img_dsc_t service_gui_view_wallpaper_image;

#endif /* GUI_SERVICE_VIEW_WALLPAPER_H */
