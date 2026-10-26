/**
  ******************************************************************************
  * @file    gui_service_config.h
  * @brief   GUI Service 的私有固定资源配置。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_CONFIG_H
#define GUI_SERVICE_CONFIG_H

/* gui_service.c */
#define SERVICE_GUI_DRAW_BUFFER_LINES      (320U)   /* 单个 LVGL 绘制缓冲包含的屏幕行数。当前为完整 320 行，两块 RGB565 缓冲均位于 SDRAM。 */

/* main/transport/gui_service_main_transport.c */
#define SERVICE_GUI_MUSIC_SKIP_NOTICE_MS   (2000U)  /* 状态行 `Skipped k file(s)` 提示的保持时长；期间再跳过则累加并重新计时（GUI-19）。按 LVGL tick 计时。 */
#define SERVICE_GUI_MUSIC_STATUS_FLASH_MS  (300U)   /* 状态行闪一次（text_opa 淡出再淡回）的总时长（GUI-19）。按 LVGL tick 计时。 */

/* view/gui_service_view_music.c、main/title/gui_service_main_title.c */
#define SERVICE_GUI_MUSIC_TITLE_WIDTH      (160)    /* Now Playing 曲名可见窗口宽度，像素；文本更宽时循环滚动（GUI-21）。 */
#define SERVICE_GUI_MUSIC_TITLE_SPEED      (24)     /* 曲名循环滚动速度，像素每秒。 */

#endif /* GUI_SERVICE_CONFIG_H */
