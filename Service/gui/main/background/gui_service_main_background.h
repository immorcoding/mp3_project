/**
  ******************************************************************************
  * @file    gui_service_main_background.h
  * @brief   Main Screen 私有局部毛玻璃 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_BACKGROUND_H
#define GUI_SERVICE_MAIN_BACKGROUND_H

#include "Service/service.h"

#include "lvgl.h"

Service_StatusTypeDef service_gui_main_background_prepare(
    const lv_img_dsc_t *clear_wallpaper);
Service_StatusTypeDef service_gui_main_background_apply(void);

#endif /* GUI_SERVICE_MAIN_BACKGROUND_H */
