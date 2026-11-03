/**
  ******************************************************************************
  * @file    spike_audio_read.h
  * @brief   音频流读一块（32 KiB）的持锁耗时统计（spike #191）：Storage Task 写，前台任务按阶段读。
  *
  * @details
  *          耗时取 Filesystem Service 内部“拿到锁之后到 ReadFile 返回”的持锁时长，不含等锁，
  *          用来对比各阶段前台负载对音频读取的影响（P1 无负载，P2 读 TXT，P3 Flash 写）。
  *          只统计整块 32 KiB 的成功读取；更新在短临界区内，窗口由前台任务在阶段开始时清零。
  ******************************************************************************
  */

#ifndef SPIKE_AUDIO_READ_H
#define SPIKE_AUDIO_READ_H

#include <stdint.h>

#include "APP/tasks/foreground/foreground_stats.h"

void spike_audio_read_note(uint32_t hold_us);
void spike_audio_read_get(ForegroundStatTypeDef *stat);
void spike_audio_read_reset(void);

#endif /* SPIKE_AUDIO_READ_H */
