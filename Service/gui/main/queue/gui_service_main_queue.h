/**
  ******************************************************************************
  * @file    gui_service_main_queue.h
  * @brief   Main Screen 私有 Queue 行复制 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_QUEUE_H
#define GUI_SERVICE_MAIN_QUEUE_H

#include <stdint.h>

#include "Service/service.h"

Service_StatusTypeDef service_gui_main_queue_prepare(void);
uint16_t service_gui_main_queue_scroll_lead(void);
Service_StatusTypeDef service_gui_main_queue_apply(
    const char **titles,
    uint16_t length,
    uint16_t window_index);

#endif /* GUI_SERVICE_MAIN_QUEUE_H */
