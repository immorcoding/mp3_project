/**
  ******************************************************************************
  * @file    storage_task.h
  * @brief   Storage Task 入口。
  ******************************************************************************
  */

#ifndef STORAGE_TASK_H
#define STORAGE_TASK_H

/** @brief 无文件系统时是否自动格式化 Flash 卷 */
#define STORAGE_FLASH_AUTO_FORMAT     1U

/**
 * @brief Storage Task 私有的任务通知槽。
 * @note  索引只在本任务的通知数组内有效；GUI Task 可独立复用数值 0。
 */
typedef enum
{
    STORAGE_NOTIFY_SD_DETECT = 0U,     /**< GPIO EXTI 卡检测边沿，由本任务主循环消抖。 */
    STORAGE_NOTIFY_SD_TRANSFER,        /**< SDMMC DMA 完成，由 Filesystem SD 执行器等待。 */
    STORAGE_NOTIFY_FLASH_OPERATION,    /**< QSPI/MDMA 异步完成，由 Filesystem Flash 执行器等待。 */
    STORAGE_NOTIFY_COUNT               /**< 本任务占用的通知槽数量，不是可等待的事件。 */
} Storage_NotifyIndexTypeDef;

void storage_task(void *handle);

#endif /* STORAGE_TASK_H */
