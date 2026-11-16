/**
  ******************************************************************************
  * @file    foreground_task_config.h
  * @brief   前台任务的私有参数：通知槽与标志位、"等通知"的哨兵值、step 耗时的告警阈值与日志节拍。
  ******************************************************************************
  */

#ifndef FOREGROUND_TASK_CONFIG_H
#define FOREGROUND_TASK_CONFIG_H

#include <stdint.h>

/* foreground_loop.c、foreground_task.c */
#define FOREGROUND_NOTIFY_FLAG_WAKE       (1UL << 0)  /* 前台任务通知槽：有人调了 `foreground_task_wake()`（GUI 写了需求、曲库发布了、应用想马上再被调用）。 */
#define FOREGROUND_NOTIFY_FLAG_VOLUME     (1UL << 1)  /* 前台任务通知槽：卷事件（卷任务或 Flash 启动任务发布了卷就绪或卷消失；任务入口登记订阅）。 */
#define FOREGROUND_NOTIFY_FLAG_EVENT_MASK (FOREGROUND_NOTIFY_FLAG_WAKE | FOREGROUND_NOTIFY_FLAG_VOLUME) /* 一次收取并清除的事件位；FLAG 只用于唤醒，醒来后各应用自己比对卷序号、重看最新需求。 */
#define FOREGROUND_WAIT_FOREVER           UINT32_MAX  /* step 返回它表示"我不需要定时再叫，等通知"；整圈都是它时任务一直阻塞。 */

/* foreground_loop.c：step 耗时的记录与告警（ARC-26）。 */
#define FOREGROUND_STEP_WARN_US           50000U      /* 单个 step 的耗时超过它（50 ms，初值，由板级验收修正 R5.6）且刷新了该应用的最长值时记 WARN，只记录不停机。 */
#define FOREGROUND_STEP_APP_MAX           8U          /* 最多记录最长耗时的应用个数；注册表行数更多时多出的不记录（不影响调用）。 */
#define FOREGROUND_MAX_LOG_PERIOD_MS      10000U      /* 最长值刷新但没超过阈值时，汇总成一行 INFO 的最小间隔（至多每 10 s 一行，不挤占 Service Log 消息池）。 */
#define FOREGROUND_LOG_LINE_BYTES         128U        /* 日志行缓冲（含结尾 NUL）；Service Log 单条正文容量也是 128 B。 */

#endif /* FOREGROUND_TASK_CONFIG_H */
