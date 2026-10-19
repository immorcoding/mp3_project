/**
  ******************************************************************************
  * @file    gui_service_main_transport.h
  * @brief   Main Screen 私有 Now Playing 三键、进度条与时间 Label（兼作状态行）Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_TRANSPORT_H
#define GUI_SERVICE_MAIN_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "Service/service.h"

Service_StatusTypeDef service_gui_main_transport_prepare(void);
Service_StatusTypeDef service_gui_main_transport_apply(bool playing);
Service_StatusTypeDef service_gui_main_transport_apply_progress(uint8_t percent);
Service_StatusTypeDef service_gui_main_transport_apply_time(uint32_t position_ms, uint32_t duration_ms);
Service_StatusTypeDef service_gui_main_transport_apply_status(const char *text);
Service_StatusTypeDef service_gui_main_transport_apply_skip_notice(uint32_t added);
Service_StatusTypeDef service_gui_main_transport_flash(void);

#endif /* GUI_SERVICE_MAIN_TRANSPORT_H */
