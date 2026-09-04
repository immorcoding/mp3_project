/**
  ******************************************************************************
  * @file    gui_service_config.h
  * @brief   GUI Service 的私有固定资源配置。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_CONFIG_H
#define GUI_SERVICE_CONFIG_H

/* gui_service.c */
#define SERVICE_GUI_DRAW_BUFFER_LINES  (320U)  /* 单个 LVGL 绘制缓冲包含的屏幕行数。当前为完整 320 行，两块 RGB565 缓冲均位于 SDRAM。 */

#endif /* GUI_SERVICE_CONFIG_H */
