/**
  ******************************************************************************
  * @file    gui_service_main_vinyl_config.h
  * @brief   Now Playing 唱盘旋转的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_VINYL_CONFIG_H
#define GUI_SERVICE_MAIN_VINYL_CONFIG_H

/* gui_service_main_vinyl.c */
#define SERVICE_GUI_MAIN_VINYL_REVOLUTION_MS  (6000U) /* 播放时唱盘顺时针转一圈的毫秒数。 */
#define SERVICE_GUI_MAIN_VINYL_FULL_TURN      (3600)  /* LVGL lv_img 角度单位为 0.1°，一圈 3600。 */

#endif /* GUI_SERVICE_MAIN_VINYL_CONFIG_H */
