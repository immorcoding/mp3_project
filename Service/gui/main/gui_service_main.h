/**
  ******************************************************************************
  * @file    gui_service_main.h
  * @brief   GUI Service 私有 Main Screen 运行时 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_H
#define GUI_SERVICE_MAIN_H

#include "Service/service.h"

#include "lvgl.h"

Service_StatusTypeDef service_gui_main_prepare(
    const lv_img_dsc_t *clear_wallpaper);
Service_StatusTypeDef service_gui_main_apply_theme(void);

#endif /* GUI_SERVICE_MAIN_H */
