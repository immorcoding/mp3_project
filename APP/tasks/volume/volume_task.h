/**
 ******************************************************************************
 * @file    volume_task.h
 * @brief   卷任务入口：SD 热插拔与挂载、卷事件发布、Flash 启动任务的创建。
 ******************************************************************************
 */

#ifndef VOLUME_TASK_H
#define VOLUME_TASK_H

#include "APP/tasks/volume/volume_event.h"
#include "APP/tasks/volume/volume_task_config.h"

/**
 * @brief 卷任务私有的任务通知槽。
 * @note  索引只在本任务的通知数组内有效；其他任务可独立复用数值 0。
 */
typedef enum
{
    VOLUME_NOTIFY_EVENT = 0U,   /**< 卡检测边沿（ISR 置位）。 */
    VOLUME_NOTIFY_COUNT         /**< 本任务占用的通知槽数量，不是可等待的事件。 */
} Volume_NotifyIndexTypeDef;

void volume_task(void *handle);

#endif /* VOLUME_TASK_H */
