/**
  ******************************************************************************
  * @file    gui_service_main_transport.h
  * @brief   Main Screen 私有 Now Playing 三键 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_TRANSPORT_H
#define GUI_SERVICE_MAIN_TRANSPORT_H

#include <stdbool.h>

#include "Service/service.h"

Service_StatusTypeDef service_gui_main_transport_prepare(void);
Service_StatusTypeDef service_gui_main_transport_apply(bool playing);

#endif /* GUI_SERVICE_MAIN_TRANSPORT_H */
