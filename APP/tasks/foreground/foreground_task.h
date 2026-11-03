/**
  ******************************************************************************
  * @file    foreground_task.h
  * @brief   前台任务入口（spike #191，可丢弃分支）。
  *
  * @details
  *          前台任务按阶段给系统施加负载并输出汇总日志，用来在板上拿“加锁、前台任务、GUI 睡眠”
  *          的实测数字。音乐处于播放状态时自动按 P0 至 P5 依次运行，暂停或停止时等待。
  *          GUI 每圈调用的格子读取入口 `foreground_gui_step()` 在 foreground_load.h。
  ******************************************************************************
  */

#ifndef FOREGROUND_TASK_H
#define FOREGROUND_TASK_H

void foreground_task(void *handle);

#endif /* FOREGROUND_TASK_H */
