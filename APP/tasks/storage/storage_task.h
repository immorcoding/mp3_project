/**
  ******************************************************************************
  * @file    storage_task.h
  * @brief   Storage Task 入口。
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
    STORAGE_NOTIFY_EVENT = 0U,         /**< 主循环事件槽：卡检测与窗口请求以 FLAG 共存。 */
    STORAGE_NOTIFY_SD_TRANSFER,        /**< SDMMC DMA 完成，由 Filesystem SD 执行器等待。 */
    STORAGE_NOTIFY_FLASH_OPERATION,    /**< QSPI/MDMA 异步完成，由 Filesystem Flash 执行器等待。 */
    STORAGE_NOTIFY_COUNT               /**< 本任务占用的通知槽数量，不是可等待的事件。 */
} Storage_NotifyIndexTypeDef;

void storage_task(void *handle);
bool storage_task_sd_is_ready(void);
bool storage_task_sd_is_mounted(void);

#endif /* STORAGE_TASK_H */
