/**
  ******************************************************************************
  * @file    app_config.h
  * @brief   应用层功能编译开关。
  *
  * @details
  *          本文件集中保存 Application 层需要了解的功能开关，避免各模块
  *          在 main.c 中散落条件编译宏。这里的宏只表达“应用是否使用该
  *          功能”，不负责配置 GPIO、总线、时钟或具体器件参数。
  *
 * @note    当前仅保留 SD 与外部 Flash 基准测试开关；需要引入其他可裁剪功能时
 *          再在本文件增加宏，避免预先维护没有实际使用者的占位配置。
  ******************************************************************************
  */
#pragma once
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define STORAGE_SD_BENCHMARK_ENABLE       0
/* 默认关闭；允许独立验证构建通过编译定义启用，不改动产品默认值。 */
#ifndef STORAGE_FLASH_BENCHMARK_ENABLE
#define STORAGE_FLASH_BENCHMARK_ENABLE    0
#endif
#define STORAGE_SDRAM_BENCHMARK_ENABLE    0

#endif /* APP_CONFIG_H */
