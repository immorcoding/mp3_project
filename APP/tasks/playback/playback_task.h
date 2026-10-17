/**
  ******************************************************************************
  * @file    playback_task.h
  * @brief   播放任务入口与私有通知槽。
  * @note    GUI 音乐分区使用的命令与快照 Interface 在 playback_contract.h，不在本头里。
  ******************************************************************************
  */

#ifndef PLAYBACK_TASK_H
#define PLAYBACK_TASK_H

#include "APP/tasks/playback/playback_task_config.h"

/**
 * @brief 播放任务私有的任务通知槽。
 * @note  索引只在本任务的通知数组内有效；与 Storage Task、GUI Task 的槽位编号互不相关。
 */
typedef enum
{
    PLAYBACK_NOTIFY_EVENT = 0U,   /**< 主循环事件槽：命令、流记录、块就绪与音频事件以 FLAG 共存。 */
    PLAYBACK_NOTIFY_COUNT         /**< 本任务占用的通知槽数量，不是可等待的事件。 */
} Playback_NotifyIndexTypeDef;

void playback_task(void *handle);

#endif /* PLAYBACK_TASK_H */
