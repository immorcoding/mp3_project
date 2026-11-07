/**
  ******************************************************************************
  * @file    storage_task.h
  * @brief   Storage Task 入口：卷事件的订阅者，曲库扫描与 Flash 回收。
  ******************************************************************************
  */

#ifndef STORAGE_TASK_H
#define STORAGE_TASK_H

#include <stdbool.h>

#include "APP/tasks/storage/storage_task_config.h"

/** @brief Storage 操作返回状态 */
typedef enum{
    STORAGE_OK = 0U,
    STORAGE_ERROR
} Storage_StatusTypeDef;

/**
 * @brief Storage Task 私有的任务通知槽。
 * @note  索引只在本任务的通知数组内有效；GUI Task 可独立复用数值 0。
 */
typedef enum{
    STORAGE_NOTIFY_EVENT = 0U,         /**< 主循环事件槽：卷事件。 */
    STORAGE_NOTIFY_COUNT               /**< 本任务占用的通知槽数量，不是可等待的事件。 */
} Storage_NotifyIndexTypeDef;

void storage_task(void *handle);

#endif /* STORAGE_TASK_H */
