/**
  ******************************************************************************
  * @file    gui_service_boot_config.h
  * @brief   GUI Service 启动轨道动画的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_BOOT_CONFIG_H
#define GUI_SERVICE_BOOT_CONFIG_H

/* gui_service_boot.c */
#define SERVICE_GUI_BOOT_ARC_CYCLE_TIME_MS      (1400U)  /* 活动弧完成一次伸长、缩短并回到起点的总周期，单位为毫秒。 */
#define SERVICE_GUI_BOOT_ARC_MIN_SWEEP_DEG      (30U)    /* 活动弧缩短时覆盖的最小角度。 */
#define SERVICE_GUI_BOOT_ARC_MAX_SWEEP_DEG      (210U)   /* 活动弧伸长时覆盖的最大角度。 */
#define SERVICE_GUI_BOOT_ARC_START_OFFSET_DEG   (255U)   /* 相对 Arc 静态角度的逆时针偏移；LVGL Arc 增大方向为顺时针。 */

#define SERVICE_GUI_BOOT_WALLPAPER_BLUR_RADIUS  (12U)    /* Boot 背景 Canvas 模糊半径；半径属 Boot 视觉参数，Canvas 不持有启动页配置。 */
#define SERVICE_GUI_BOOT_HOLD_TIME_MS           (4000U)  /* Boot（模糊壁纸 + 启动环）保持的时间，单位为毫秒。 */
#define SERVICE_GUI_BOOT_REVEAL_FADE_TIME_MS    (400U)   /* Boot 淡出到 BootReveal（清晰壁纸）的时长，单位为毫秒。 */
#define SERVICE_GUI_BOOT_REVEAL_HOLD_TIME_MS    (200U)   /* 清晰壁纸在 BootReveal 中保持可见的时间，单位为毫秒。 */
#define SERVICE_GUI_BOOT_LOCK_FADE_TIME_MS      (600U)   /* BootReveal 异步切入 Lock 时的 Fade 时长，单位为毫秒。 */

#endif /* GUI_SERVICE_BOOT_CONFIG_H */
