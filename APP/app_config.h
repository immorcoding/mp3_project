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
#ifndef STORAGE_SD_BENCHMARK_ENABLE
#define STORAGE_SD_BENCHMARK_ENABLE     0      /* 产品默认关闭 SD 文件读写基准；独立验证构建（含 Tests/volume 的顺序用例）可 -D 打开。 */
#endif
#ifndef STORAGE_FLASH_BENCHMARK_ENABLE
#define STORAGE_FLASH_BENCHMARK_ENABLE  0      /* 产品默认关闭 Flash 基准；独立验证构建可 -D 打开。 */
#endif
#define STORAGE_SDRAM_BENCHMARK_ENABLE  0      /* 产品默认关闭 SDRAM 破坏性自检。 */

/* app_tasks.h */
#define APP_BOOT_TASK_STACK_WORDS       192U   /* Boot Task 栈深度，单位为 word（768 B）。#220 从 128 word 提到 192 word（用户 2026-11-06 的决定，多占 256 B）：#218 静态核算余量只剩约 76 B；最深调用链和余量的核算见 APP/tasks/volume/README.md“默认构建的 Boot 栈核算”。打开可选的 SDRAM 自检（STORAGE_SDRAM_BENCHMARK_ENABLE）仍装不下，app_tasks.c 有编译期断言。 */
#define APP_LOG_TASK_STACK_WORDS        512U   /* Log Task 栈深度，单位为 word。 */
#define APP_AUDIO_READER_TASK_NAME      "Audio Read Task" /* 音频读取任务的名字，只在这里定义一次：创建（audio_reader_task.c）和 Monitor 的登记表（monitor_task.c）都引用它。最长 15 个字符（configMAX_TASK_NAME_LEN 为 16），“Audio Reader Task”有 17 个，会被截断成登记不上的名字；audio_reader_task.c 有编译期断言。 */
#define APP_AUDIO_READER_TASK_STACK_WORDS 1024U /* Audio Read Task 栈深度，单位为 word（4 KiB）；静态创建，栈位于 AXI SRAM 新段 .app_axi_ram（与卷 4 KiB、Flash 启动 4 KiB、后台 6 KiB、前台 6 KiB 共 24 KiB）。读取任务只读文件、填块池，不解码；第 4 次上板最低剩余 2048 至 2688 B（R4.11）。初值，由板级验收修正。 */
#define APP_FOREGROUND_TASK_NAME        "Foreground Task" /* 前台任务的名字，只在这里定义一次：创建（foreground_task.c）和 Monitor 的登记表（monitor_task.c）都引用它。正好 15 个字符（configMAX_TASK_NAME_LEN 为 16）；foreground_task.c 有编译期断言。CPU 占比行的短键是 `fore`。 */
#define APP_FOREGROUND_TASK_STACK_WORDS 1536U  /* Foreground Task 栈深度，单位为 word（6 KiB）；静态创建，栈位于 AXI SRAM 新段 .app_axi_ram。所有应用的前台 step 共用它，step 里不放大局部变量。初值，由板级验收修正（R5.7）。 */
#define APP_VOLUME_TASK_STACK_WORDS     1024U  /* Volume Task 栈深度，单位为 word（4 KiB）；静态创建，栈位于 AXI SRAM 新段 .app_axi_ram。初值，由板级验收修正。 */
#define APP_FLASH_BOOT_TASK_STACK_WORDS 1024U  /* Flash 启动任务栈深度，单位为 word（约 4 KiB）；一次性，静态创建，栈位于 AXI SRAM 新段 .app_axi_ram。初值，由板级验收修正。 */
#define APP_BACKGROUND_TASK_STACK_WORDS 1536U  /* Background Task 栈深度，单位为 word（6 KiB）；静态创建，栈位于 AXI SRAM 新段 .app_axi_ram。初值，由板级验收修正。 */
#define APP_MONITOR_TASK_STACK_WORDS    256U   /* Monitor Task 栈深度，单位为 word。 */
#define APP_GUI_TASK_STACK_WORDS        2048U  /* GUI Task 栈深度，单位为 word。768 word 会在 Default 解锁 Fade 时溢出。 */
#define APP_PLAYBACK_TASK_STACK_WORDS   5120U  /* Playback Task 栈深度，单位为 word（20480 B）；静态创建，栈与 TCB 位于 DTCM .bss。 */

#define APP_BOOT_TASK_PRIORITY          0U     /* Boot Task 优先级。 */
#define APP_LOG_TASK_PRIORITY           2U     /* Log Task 优先级（#218：1 → 2）：与 GUI、前台、Monitor 同级，日志不被它们饿着。 */
#define APP_AUDIO_READER_TASK_PRIORITY  3U     /* Audio Read Task 优先级：读文件填块池；与卷任务、定时器服务任务同级（时间片轮转），低于播放（4）、高于前台、GUI、Log、Monitor（2）与后台、Flash 启动（1）。到安全水位后每读一块用等待点让 1 tick，让的是 SD 锁。 */
#define APP_VOLUME_TASK_PRIORITY        3U     /* Volume Task 优先级：消抖、挂载卸载、发布卷事件；与定时器服务任务（configTIMER_TASK_PRIORITY = 3）同级。 */
#define APP_FLASH_BOOT_TASK_PRIORITY    1U     /* Flash 启动任务优先级：与后台任务同级（#220，2 → 1，用户 2026-11-07 选的方案 A）。低于卷任务，MountFlash 期间插拔卡照常响应；低于前台、GUI、Log、Monitor（2），所以开机前约 8 s 它不再抢走优先级 1 的曲库扫描的 CPU（第 5 次上板：它占约 77 %，扫描 9.0 s），而是和后台任务轮流。代价是 Flash 就绪晚约 1.5 至 2 s（大目录，常规库几乎没有）；Tests/background_task 的 test_task_priorities.c 钉住这个排序。 */
#define APP_FOREGROUND_TASK_PRIORITY    2U     /* Foreground Task 优先级：轮流调各应用的前台 step；与 GUI、Log、Monitor 同级（时间片轮转），低于音频读取与卷（3），高于后台与 Flash 启动任务（1）。 */
#define APP_BACKGROUND_TASK_PRIORITY    1U     /* Background Task 优先级：与 Flash 启动任务同级（时间片轮转）；低于 GUI、前台、Log、Monitor（2），只拿空隙，不抢前台。 */
#define APP_MONITOR_TASK_PRIORITY       2U     /* Monitor Task 优先级（#218：1 → 2）。 */
#define APP_GUI_TASK_PRIORITY           2U     /* GUI Task 优先级（#218：1 → 2）：每圈睡眠，没活就让出；与前台任务同级时按时间片轮转，`ui_cell` 的短锁持锁只拷内存。 */
#define APP_PLAYBACK_TASK_PRIORITY      4U     /* Playback Task 优先级：播放 4 > 音频读取、卷 3（= 定时器服务 3）> 前台、GUI、Log、Monitor 2 > 后台、Flash 启动 1 > Boot 0。 */

#endif /* APP_CONFIG_H */
