/**
  ******************************************************************************
  * @file    monitor_task_config.h
  * @brief   Monitor Task 的私有周期与输出缓冲参数。
  ******************************************************************************
  */

#ifndef MONITOR_TASK_CONFIG_H
#define MONITOR_TASK_CONFIG_H

/* monitor_task.c */
#define MONITOR_TASK_PERIOD_MS      500U  /* Monitor Task 的基础调度周期，单位为毫秒。 */
#define MONITOR_SNAPSHOT_INTERVAL   10U   /* 经过多少个基础周期后输出一次任务栈快照。 */
#define MONITOR_LOG_MESSAGE_LENGTH  64U   /* 单条任务栈监控日志的本地格式化缓冲区长度。 */

/* monitor_stats.c、monitor_task.c */
#define MONITOR_LOG_LINE_LENGTH     128U  /* CPU 占比与健康行的本地格式化缓冲区长度；Service Log 单条正文容量也是 128 B（含 NUL）。 */
#define MONITOR_CPU_TASK_MAX        16U   /* CPU 占比基准与快照最多容纳的任务数；任务更多时多出的不记录。 */

/* monitor_warn.c */
#define MONITOR_WARNED_TASK_MAX     8U    /* 未登记任务告警去重记录可容纳的任务名个数；写满后新名字不再告警。 */
#define MONITOR_WARNED_NAME_LENGTH  16U   /* 每条记录保存的任务名缓冲长度（含结尾 NUL），与 configMAX_TASK_NAME_LEN 对齐；更长的名字被截断后比较。 */

#endif /* MONITOR_TASK_CONFIG_H */
