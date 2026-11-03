/**
  ******************************************************************************
  * @file    foreground_report.h
  * @brief   前台任务的日志汇总：标签统一 SPIKE，按 10 s 窗口和阶段结束各输出一组稳定的行（spike #191）。
  *
  * @details
  *          每条日志不超过 Service Log 的 128 B 单条上限，行首第一个词固定（CPU、GUI、LOCK、LOCKBLK、
  *          SDREAD、FLASHIO、UNDER、CELL、CELLGUI、GUARD、UNPLUG、PHASE、INIT），方便 grep。
  *          不记每帧或每次调用的日志（DIAG-3）；每行之后让出几毫秒，不让汇总挤满 30 条的消息池，
  *          也不挤掉 PLAYBACK 等日志。
  ******************************************************************************
  */

#ifndef FOREGROUND_REPORT_H
#define FOREGROUND_REPORT_H

#include <stdbool.h>

#include "APP/tasks/foreground/foreground_load.h"

void foreground_logf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void foreground_report_baseline(void);
void foreground_report_window(const char *phase, ForegroundLoadKindTypeDef kind, bool final);
void foreground_report_unplug(void);

#endif /* FOREGROUND_REPORT_H */
