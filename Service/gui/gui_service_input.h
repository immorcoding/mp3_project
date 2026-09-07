/**
  ******************************************************************************
  * @file    gui_service_input.h
  * @brief   GUI Service 私有输入单槽。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_INPUT_H
#define GUI_SERVICE_INPUT_H

#include "Service/gui/gui_service.h"

void service_gui_input_post(
    Service_GUI_InputCommandTypeDef command,
    uint16_t param);
void service_gui_input_drop_queue_select(void);
Service_StatusTypeDef service_gui_input_consume(Service_GUI_InputTypeDef *input);

#endif /* GUI_SERVICE_INPUT_H */
