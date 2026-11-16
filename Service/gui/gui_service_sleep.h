/**
  ******************************************************************************
  * @file    gui_service_sleep.h
  * @brief   GUI Service 私有睡眠时长计算（不依赖 LVGL 与 FreeRTOS）。
  *
  * @details
  *          `Service_GUI_Process()` 把 `lv_timer_handler()` 的返回值、本圈耗时和
  *          输入槽状态交给 `service_gui_sleep_ms()`，结果作为它自己的返回值交给
  *          GUI Task 的 `vTaskDelay`。仅 `Service/gui` 内部使用，APP 不得包含。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_SLEEP_H
#define GUI_SERVICE_SLEEP_H

#include <stdbool.h>
#include <stdint.h>

uint32_t service_gui_sleep_ms(
    uint32_t timer_ms,
    uint32_t elapsed_ms,
    bool input_pending);

#endif /* GUI_SERVICE_SLEEP_H */
