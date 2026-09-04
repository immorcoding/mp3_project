/**
  ******************************************************************************
  * @file    gui_service_boot_config.h
  * @brief   GUI Service 启动轨道动画的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_BOOT_CONFIG_H
#define GUI_SERVICE_BOOT_CONFIG_H

#define SERVICE_GUI_BOOT_ARC_CYCLE_TIME_MS     (1400U) /* 活动弧完成一次“伸长、缩短、前端恰好回到起点”的总周期，单位为毫秒。 一条相位动画同时计算活动弧的两端，避免缩短时前端反向移动。 */

#define SERVICE_GUI_BOOT_ARC_MIN_SWEEP_DEG     (30U) /* 活动弧缩短时覆盖的最小角度。 */

#define SERVICE_GUI_BOOT_ARC_MAX_SWEEP_DEG     (210U) /* 活动弧伸长时覆盖的最大角度。 */

#define SERVICE_GUI_BOOT_ARC_START_OFFSET_DEG  (255U) /* 活动弧相对 SquareLine 静态预览的逆时针偏移角度。 LVGL Arc 角度增大方向为屏幕顺时针，故逆时针 90 度表示 270 度。 */

#define SERVICE_GUI_BOOT_WALLPAPER_BLUR_RADIUS  (12U) /* Boot 背景使用 Canvas 生成模糊副本时的横向、纵向模糊半径。 半径属于 Boot 的视觉参数；Canvas Module 只接收调用方提供的半径， 不持有任何启动页专属配置。 */

#define SERVICE_GUI_BOOT_REVEAL_HOLD_TIME_MS    (200U) /* 清晰壁纸在 BootReveal 中保持可见的时间，单位为毫秒。 */

#define SERVICE_GUI_BOOT_LOCK_FADE_TIME_MS      (600U) /* BootReveal 异步切入 Lock 时的 Fade 时长，单位为毫秒。 */

#endif /* GUI_SERVICE_BOOT_CONFIG_H */
