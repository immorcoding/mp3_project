/**
  ******************************************************************************
  * @file    gui_service.h
  * @brief   GUI Service 公共 Interface。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_H
#define GUI_SERVICE_H

#include "Service/service.h"

Service_StatusTypeDef Service_GUI_Init(void);
void Service_GUI_Process(void);

#endif /* GUI_SERVICE_H */
