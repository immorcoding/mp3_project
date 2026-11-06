/**
  ******************************************************************************
  * @file    storage_flash.h
  * @brief   Storage Task 内部的 Flash 空闲回收接口。
  *
  * @details
  *          Flash 卷的启动、挂载和格式化策略已经移到 `APP/tasks/volume/flash/`（一次性的 Flash 启动任务，
  *          发布 Flash 卷就绪）。本模块只剩回收：回收放进后台任务之前，仍由 Storage Task 在卷就绪后调用。
  ******************************************************************************
  */

#ifndef STORAGE_FLASH_H
#define STORAGE_FLASH_H

#include "APP/tasks/storage/storage_task.h"

Storage_StatusTypeDef storage_flash_reclaim(void);

#endif /* STORAGE_FLASH_H */
