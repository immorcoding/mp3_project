/**
  ******************************************************************************
  * @file    gui_service_view_wallpaper.c
  * @brief   默认系统壁纸的 LVGL 图片描述符。
  *
  * @details
  *          像素不随固件存放：固件中由 gui_service_view_wallpaper_region.c 在
  *          SDRAM 资源区预留，启动时 Service/resource 从资源包装入；模拟器由替身
  *          提供同名像素数组。本文件只描述格式与尺寸。
  ******************************************************************************
  */

#include "Service/gui/view/gui_service_view_wallpaper.h"

const lv_img_dsc_t service_gui_view_wallpaper_image = {
    .header.always_zero = 0,
    .header.w = SERVICE_GUI_VIEW_WALLPAPER_WIDTH,
    .header.h = SERVICE_GUI_VIEW_WALLPAPER_HEIGHT,
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .data_size = SERVICE_GUI_VIEW_WALLPAPER_BYTES,
    .data = service_gui_view_wallpaper_pixels,
};
