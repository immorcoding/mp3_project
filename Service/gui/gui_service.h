/**
  ******************************************************************************
  * @file    gui_service.h
  * @brief   GUI Service 公共 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_H
#define GUI_SERVICE_H

#include <stdint.h>
#include "Service/service.h"

Service_StatusTypeDef Service_GUI_Init(uint32_t notify_index);
void Service_GUI_Process(void);
uint16_t Service_GUI_QueueScrollLead(void);
Service_StatusTypeDef Service_GUI_QueueApply(
    const char **titles,
    uint16_t length,
    uint16_t window_index);

#endif /* GUI_SERVICE_H */
