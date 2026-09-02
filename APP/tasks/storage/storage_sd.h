/**
  ******************************************************************************
  * @file    storage_sd.h
  * @brief   Storage Task 内部的 SD 介质生命周期接口。
  *
  * @details
  *          本模块集中 Platform SD 生命周期、FatFs 挂载状态和相关日志。
  *          调用者只能是 Storage Task；它不向其他应用任务暴露直接访问
  *          文件系统的接口。
  ******************************************************************************
  */

#ifndef STORAGE_SD_H
#define STORAGE_SD_H

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

void storage_sd_init(TaskHandle_t task_handle);

void storage_sd_process(void);

#endif /* STORAGE_SD_H */
