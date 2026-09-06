/**
  ******************************************************************************
  * @file    gui_service_theme_apply.h
  * @brief   GUI 调色板的 LVGL 过滤器挂接。
  *
  * @details
 *          仅供 gui_service.c 在 ui_init() 之后调用：生成代码会覆盖 display theme。
 *          APP 不得包含本文件。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_THEME_APPLY_H
#define GUI_SERVICE_THEME_APPLY_H

#include "Service/service.h"

#include "lvgl.h"

Service_StatusTypeDef service_gui_theme_attach(lv_disp_t *disp);
void service_gui_theme_bind_tree(lv_obj_t *root);
void service_gui_theme_bind_screens(void);

#endif /* GUI_SERVICE_THEME_APPLY_H */
