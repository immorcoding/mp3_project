/**
  ******************************************************************************
  * @file    gui_service_main_title.h
  * @brief   Main Screen 私有 Now Playing 曲名 Label Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_TITLE_H
#define GUI_SERVICE_MAIN_TITLE_H

#include "Service/service.h"

Service_StatusTypeDef service_gui_main_title_prepare(void);
Service_StatusTypeDef service_gui_main_title_apply(const char *text);

#endif /* GUI_SERVICE_MAIN_TITLE_H */
