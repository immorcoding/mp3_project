/**
  ******************************************************************************
  * @file    gui_service_view_screens.h
  * @brief   view/ 内部各 Screen 构建函数；仅 gui_service_view.c 调用。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_VIEW_SCREENS_H
#define GUI_SERVICE_VIEW_SCREENS_H

#include "Service/gui/view/gui_service_view.h"

#include "lvgl.h"

void service_gui_view_boot_create(Service_GUI_ViewTypeDef *view, const lv_img_dsc_t *wallpaper);
void service_gui_view_lock_create(Service_GUI_ViewTypeDef *view, const lv_img_dsc_t *wallpaper);
void service_gui_view_main_create(Service_GUI_ViewTypeDef *view, const lv_img_dsc_t *wallpaper);
void service_gui_view_music_create(Service_GUI_ViewTypeDef *view, lv_obj_t *page);

#endif /* GUI_SERVICE_VIEW_SCREENS_H */
