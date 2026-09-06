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

/* app_config.h */
#define STORAGE_SD_BENCHMARK_ENABLE     0      /* 产品默认关闭 SD 文件读写基准。 */
#ifndef STORAGE_FLASH_BENCHMARK_ENABLE
#define STORAGE_FLASH_BENCHMARK_ENABLE  0      /* 产品默认关闭 Flash 基准；独立验证构建可 -D 打开。 */
#endif
#define STORAGE_SDRAM_BENCHMARK_ENABLE  0      /* 产品默认关闭 SDRAM 破坏性自检。 */

/* app_tasks.h */
#define APP_BOOT_TASK_STACK_WORDS       128U   /* Boot Task 栈深度，单位为 word。 */
#define APP_LOG_TASK_STACK_WORDS        512U   /* Log Task 栈深度，单位为 word。 */
#define APP_STORAGE_TASK_STACK_WORDS    1024U   /* Storage Task 栈深度，单位为 word。 */
#define APP_MONITOR_TASK_STACK_WORDS    256U   /* Monitor Task 栈深度，单位为 word。 */
#define APP_GUI_TASK_STACK_WORDS        2048U  /* GUI Task 栈深度，单位为 word。768 word 会在 Default 解锁 Fade 时溢出。 */

#define APP_BOOT_TASK_PRIORITY          0U     /* Boot Task 优先级。 */
#define APP_LOG_TASK_PRIORITY           1U     /* Log Task 优先级。 */
#define APP_STORAGE_TASK_PRIORITY       2U     /* Storage Task 优先级。 */
#define APP_MONITOR_TASK_PRIORITY       1U     /* Monitor Task 优先级。 */
#define APP_GUI_TASK_PRIORITY           1U     /* GUI Task 优先级。 */

#endif /* APP_CONFIG_H */
