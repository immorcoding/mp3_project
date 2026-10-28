/**
  ******************************************************************************
  * @file    gui_service_font.h
  * @brief   GUI Service 私有的曲目文本字体子模块：启动时解码可选 FONT 资源，否则回退 Montserrat。
  *          唱盘页曲名与 Queue 曲名、歌手都取 service_gui_font_title()（GUI-24）。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_FONT_H
#define GUI_SERVICE_FONT_H

#include "lvgl.h"

void service_gui_font_init(void);
const lv_font_t *service_gui_font_title(void);

#endif /* GUI_SERVICE_FONT_H */
