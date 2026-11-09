/**
  ******************************************************************************
  * @file    monitor_task_config.h
  * @brief   Monitor Task 的私有周期与输出缓冲参数。
  ******************************************************************************
  */

#ifndef MONITOR_TASK_CONFIG_H
#define MONITOR_TASK_CONFIG_H

/* monitor_task.c */
#define MONITOR_TASK_PERIOD_MS          500U            /* Monitor Task 的基础调度周期，单位为毫秒。 */
#define MONITOR_SNAPSHOT_INTERVAL       10U             /* 经过多少个基础周期后输出一次任务栈快照。 */
#define MONITOR_LOG_MESSAGE_LENGTH      64U             /* 单条任务栈监控日志的本地格式化缓冲区长度。 */

/* monitor_task.c：Service Log 消息池的最坏同时到达核算（DIAG-3；保守口径见 APP/tasks/audio_reader/README.md，#218 重算）。 */
#define MONITOR_TASK_CONCURRENT_MAX     11U             /* 同一批里最多同时存在的任务数：永久任务 10 个（IDLE、Tmr Svc、Log、Audio Read、Volume、Playback、Monitor、GUI、Background、Foreground）加开机后几秒内才有的 Flash 启动任务；`Create Task` 创建完就自删，早于第一批。#220 删掉 Storage Task，少一个。 */
#define MONITOR_POOL_OTHER_WORST_LINES  18U             /* 同一时刻最坏还能到来的其他任务日志行数：Filesystem 持锁统计 3 + 曲库短锁 1 + `ui_cell` 持锁统计 1 + 显示延迟汇总 1 + 前台 step 耗时 1（WARN 或最长值汇总）+ 一次拔卡或插卡 8（拔卡：卷路径日志 5、读取被打断、`Stop reason`、`CARD_REMOVED lat`；插卡或拔卡发生在扫描中时没有音频流，是卷路径 5 加扫描最多 3 行——节流后的第一条 WARN、一条 ERROR、`Scan done`，仍在这 8 行之内，#220）+ `SERVICE_BUSY` 2（音频读取任务与前台任务各 1 行）+ Flash 回收失败 1（后台任务的回收作业，失败一次就停，所以只有 1 条，#220）。 */
#define MONITOR_POOL_SPARE_MIN          8U              /* 以上最坏情况同时到达后消息池至少还要留的块数（用户 2026-11-06 决定）。 */

/* monitor_stats.c、monitor_task.c */
#define MONITOR_LOG_LINE_LENGTH         128U            /* CPU 占比与健康行的本地格式化缓冲区长度；Service Log 单条正文容量也是 128 B（含 NUL）。 */
#define MONITOR_CPU_TASK_MAX            16U             /* CPU 占比基准与快照最多容纳的任务数；任务更多时多出的不记录。 */

/* monitor_warn.c */
#define MONITOR_WARNED_TASK_MAX         8U              /* 未登记任务告警去重记录可容纳的任务名个数；写满后新名字不再告警。 */
#define MONITOR_WARNED_NAME_LENGTH      16U             /* 每条记录保存的任务名缓冲长度（含结尾 NUL），与 configMAX_TASK_NAME_LEN 对齐；更长的名字被截断后比较。 */

#endif /* MONITOR_TASK_CONFIG_H */
