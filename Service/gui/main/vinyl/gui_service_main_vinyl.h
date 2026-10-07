/**
 ******************************************************************************
 * @file    gui_service_main_vinyl.h
 * @brief   Main Screen 私有 Now Playing 唱盘绑定与旋转 Interface。
 ******************************************************************************
 */

#ifndef GUI_SERVICE_MAIN_VINYL_H
#define GUI_SERVICE_MAIN_VINYL_H

#include <stdbool.h>

#include "Service/service.h"

Service_StatusTypeDef service_gui_main_vinyl_prepare(void);
Service_StatusTypeDef service_gui_main_vinyl_apply(bool playing, bool reset_angle);

#endif /* GUI_SERVICE_MAIN_VINYL_H */
