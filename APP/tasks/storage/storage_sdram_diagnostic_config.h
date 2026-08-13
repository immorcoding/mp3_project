/**
  ******************************************************************************
  * @file    storage_sdram_diagnostic_config.h
  * @brief   Storage Task 启动阶段 SDRAM 硬件诊断开关。
  ******************************************************************************
  */

#ifndef STORAGE_SDRAM_DIAGNOSTIC_CONFIG_H
#define STORAGE_SDRAM_DIAGNOSTIC_CONFIG_H

/**
 * @brief 是否在 Storage Task 启动时执行破坏性的 32 MiB SDRAM 读写与测速。
 * @note  该测试完成后 SDRAM 内容无效。接入 LVGL 帧缓冲、外部堆或业务缓存后，
 *        必须改为 0，或在任何使用者开始前保证独占执行。
 */
#define STORAGE_SDRAM_DIAGNOSTIC_ENABLE  1

#endif /* STORAGE_SDRAM_DIAGNOSTIC_CONFIG_H */
