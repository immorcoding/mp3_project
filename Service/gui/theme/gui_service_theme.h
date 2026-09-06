/**
  ******************************************************************************
  * @file    gui_service_theme.h
  * @brief   GUI Service 私有调色板：占位 hex 映射与当前外观索引。
  *
  * @details
  *          本头不含 LVGL。主机测试只编译调色板 Implementation。过滤器与
  *          Lock/Main 壁纸副作用见同目录的 display 头。APP 不得包含本文件。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_THEME_H
#define GUI_SERVICE_THEME_H

#include <stdbool.h>
#include <stdint.h>

#include "Service/gui/gui_service.h"
#include "Service/service.h"

uint8_t service_gui_theme_get_current(void);
Service_StatusTypeDef service_gui_theme_select(uint8_t id);
uint32_t service_gui_theme_map_placeholder(uint32_t hex);
bool service_gui_theme_uses_wallpaper(void);
bool service_gui_theme_uses_glass(void);

#endif /* GUI_SERVICE_THEME_H */
