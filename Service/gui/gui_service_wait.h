/**
  ******************************************************************************
  * @file    gui_service_wait.h
  * @brief   GUI Service 私有：每圈结束后睡眠时长的纯计算（#190），不依赖 LVGL 与 FreeRTOS。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_WAIT_H
#define GUI_SERVICE_WAIT_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 每圈至少让出一次的睡眠下限，单位为毫秒（只睡到下一个 tick）。 */
#define SERVICE_GUI_WAIT_MIN_MS 1U

uint32_t service_gui_wait_ms(uint32_t handler_ms,
                             uint32_t elapsed_ms,
                             bool input_pending,
                             uint32_t max_ms);

#endif /* GUI_SERVICE_WAIT_H */
