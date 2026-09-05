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

#include <stdbool.h>

#include "APP/tasks/storage/storage_task.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

typedef enum
{
    STORAGE_SD_EVENT_INSERTED,
    STORAGE_SD_EVENT_REMOVED,
    STORAGE_SD_EVENT_ERROR
} Storage_SD_EventTypeDef;

Storage_StatusTypeDef storage_sd_init(TaskHandle_t task_handle);
Storage_StatusTypeDef storage_sd_process(void);
bool storage_sd_volume_is_mounted(void);

#endif /* STORAGE_SD_H */
