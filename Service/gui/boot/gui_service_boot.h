/**
  ******************************************************************************
  * @file    gui_service_boot.h
  * @brief   GUI Service 私有启动视觉序列 Interface。
  *
  * @details
  *          该头仅供 Service/gui 的生命周期 Module 调用。它不暴露给 APP 或
  *          其他 Service；它收纳 Boot 背景准备、Arc 动画，以及 Boot → BootReveal
  *          → Lock 的切屏时序。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_BOOT_H
#define GUI_SERVICE_BOOT_H

#include "Service/service.h"

#include "lvgl.h"

Service_StatusTypeDef service_gui_boot_prepare_background(
    const lv_img_dsc_t *clear_wallpaper);

void service_gui_boot_start(void);

#endif /* GUI_SERVICE_BOOT_H */
