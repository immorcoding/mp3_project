/**
  ******************************************************************************
  * @file    gui_service_boot_config.h
  * @brief   GUI Service 启动轨道动画的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_BOOT_CONFIG_H
#define GUI_SERVICE_BOOT_CONFIG_H

/**
 * @brief 活动弧完成一次“伸长、缩短、前端恰好回到起点”的总周期，单位为毫秒。
 * @note  一条相位动画同时计算活动弧的两端，避免缩短时前端反向移动。
 */
#define SERVICE_GUI_BOOT_ARC_CYCLE_TIME_MS     (1400U)

/** @brief 活动弧缩短时覆盖的最小角度。 */
#define SERVICE_GUI_BOOT_ARC_MIN_SWEEP_DEG     (30U)

/** @brief 活动弧伸长时覆盖的最大角度。 */
#define SERVICE_GUI_BOOT_ARC_MAX_SWEEP_DEG     (210U)

/**
 * @brief 活动弧相对 SquareLine 静态预览的逆时针偏移角度。
 * @note  LVGL Arc 角度增大方向为屏幕顺时针，故逆时针 90 度表示 270 度。
 */
#define SERVICE_GUI_BOOT_ARC_START_OFFSET_DEG  (255U)

#endif /* GUI_SERVICE_BOOT_CONFIG_H */
