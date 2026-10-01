/**
  ******************************************************************************
  * @file    gui_service_view_wallpaper_region.c
  * @brief   固件中默认壁纸像素的 SDRAM 资源区占位。
  *
  * @details
  *          链接脚本按输入段名 `.rodata.ui_img_wallpaper_indigo_mist_soft_dark_png_data`
  *          把本数组放进 NOLOAD 的外部资源区，并以 `__external_resource_wallpaper_*`
  *          标出起止、断言大小为 0x38400；Service/resource 启动时把资源包中的壁纸
  *          拷入这里。段名沿用旧 SquareLine 导出的符号名，以免改动链接脚本。
  *          模拟器不编译本文件，改由替身提供像素。
  ******************************************************************************
  */

#include "Service/gui/view/gui_service_view_wallpaper.h"

#include "Platform/platform.h"

uint8_t service_gui_view_wallpaper_pixels[SERVICE_GUI_VIEW_WALLPAPER_BYTES]
    __attribute__((section(".rodata.ui_img_wallpaper_indigo_mist_soft_dark_png_data"),
                   aligned(PLATFORM_DCACHE_LINE_SIZE)));
