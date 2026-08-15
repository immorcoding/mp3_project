/**
  ******************************************************************************
  * @file    lcd_task_config.h
  * @brief   LCD Task 的私有亮屏诊断参数。
  *
  * @details
  *          当前纯色测试仅用于最小硬件验收。接入 LVGL 或正式 UI 后，应按
  *          产品诊断需求保留、替换或通过 APP 功能开关禁用该测试。
  ******************************************************************************
  */

#ifndef LCD_TASK_CONFIG_H
#define LCD_TASK_CONFIG_H

/** @brief 红、绿、蓝、白每种纯色在屏幕上保持的时间，单位为毫秒。 */
#define LCD_TASK_COLOR_HOLD_PERIOD_MS     1000u
// /** @brief LCD 诊断完成后的低频空闲周期，单位为毫秒。 */
// #define LCD_TASK_IDLE_PERIOD_MS           1000u
#define LCD_TASK_REFRESH_PERIOD_MS           15u
/** @brief 单条 LCD 诊断日志的本地格式化缓冲区长度。 */
#define LCD_TASK_LOG_MESSAGE_LENGTH       64u
/** @brief RGB565 纯红色。 */
#define LCD_TASK_COLOR_RED                0xF800u
/** @brief RGB565 纯绿色。 */
#define LCD_TASK_COLOR_GREEN              0x07E0u
/** @brief RGB565 纯蓝色。 */
#define LCD_TASK_COLOR_BLUE               0x001Fu
/** @brief RGB565 纯白色。 */
#define LCD_TASK_COLOR_WHITE              0xFFFFu
/** @brief RGB565 黑色，用于执行一次画点路径。 */
#define LCD_TASK_COLOR_BLACK              0x0000u

#endif /* LCD_TASK_CONFIG_H */
