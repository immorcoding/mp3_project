/**
  ******************************************************************************
  * @file    gui_task.c
  * @brief   GUI Task 的 FreeRTOS 运行循环。
  *
  * @details
  *          本任务是 LVGL 的唯一执行上下文。它先初始化 GUI Service，随后持续
  *          推进 LVGL 定时器、绘制与输入处理；SPI DMA 刷新期间的等待由 GUI
  *          Service 注册给 LVGL 的 wait callback 完成。
  ******************************************************************************
  */

#include "gui_task.h"

#include "Service/gui/gui_service.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

void gui_task(void *handle)
{
    (void)handle;
    if (Service_GUI_Init() != SERVICE_OK)
    {
        Error_Handler();
    }

    while (1)
    {
        Service_GUI_Process();
    }
}
