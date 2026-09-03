/**
  ******************************************************************************
  * @file    storage_flash.h
  * @brief   Storage Task 内部的 Flash 卷启动、挂载策略与空闲回收接口。
  *
  * @details
  *          本模块持有 Flash 的启动顺序、未格式化告警、诊断基准时机和回收
  *          失败日志；不转发 Service 的逐个 API，也不向其他任务公开文件访问。
  *          调用必须发生在传入的 Storage Task 上下文中。
  ******************************************************************************
  */

#ifndef STORAGE_FLASH_H
#define STORAGE_FLASH_H

#include "APP/tasks/storage/storage_task.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

Storage_StatusTypeDef storage_flash_init(TaskHandle_t task_handle);

Storage_StatusTypeDef storage_flash_reclaim(void);

#endif /* STORAGE_FLASH_H */
