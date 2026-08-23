/**
  ******************************************************************************
  * @file    gui_service_config.h
  * @brief   GUI Service 的私有固定资源配置。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_CONFIG_H
#define GUI_SERVICE_CONFIG_H

/**
 * @brief 单个 LVGL 绘制缓冲包含的屏幕行数。
 * @note  当前配置为完整 320 行，因此两块 RGB565 绘制缓冲均位于 SDRAM。
 */
#define SERVICE_GUI_DRAW_BUFFER_LINES  (320U)

#endif /* GUI_SERVICE_CONFIG_H */
