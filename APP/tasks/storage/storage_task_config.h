/**
  ******************************************************************************
  * @file    storage_task_config.h
  * @brief   Storage Task 的私有通知标志与回收周期。
  ******************************************************************************
  */

#ifndef STORAGE_TASK_CONFIG_H
#define STORAGE_TASK_CONFIG_H

/* storage_task.h */
#define STORAGE_NOTIFY_FLAG_VOLUME       (1UL << 0) /* 主循环事件槽：卷事件（卷任务或 Flash 启动任务发布了卷就绪或卷消失；任务入口登记订阅）。 */
#define STORAGE_NOTIFY_FLAG_LISTBUFFER   (1UL << 1) /* 主循环事件槽：Queue 窗口 PENDING。 */
#define STORAGE_NOTIFY_FLAG_STREAM_REQUEST (1UL << 2) /* 主循环事件槽：播放任务写了音频流请求信箱。 */
#define STORAGE_NOTIFY_FLAG_BLOCK_RETURNED (1UL << 3) /* 主循环事件槽：播放任务归还了音频块。 */
#define STORAGE_NOTIFY_FLAG_NAMEBUFFER   (1UL << 4) /* 主循环事件槽：曲目显示名槽 PENDING。 */
#define STORAGE_NOTIFY_FLAG_EVENT_MASK   (STORAGE_NOTIFY_FLAG_VOLUME | \
                                          STORAGE_NOTIFY_FLAG_LISTBUFFER | \
                                          STORAGE_NOTIFY_FLAG_STREAM_REQUEST | \
                                          STORAGE_NOTIFY_FLAG_BLOCK_RETURNED | \
                                          STORAGE_NOTIFY_FLAG_NAMEBUFFER) /* 主循环一次收取并清除的事件位。 */

/* storage_task.c */
#define STORAGE_FLASH_RECLAIM_PERIOD_MS  100U  /* 空闲时回收机会周期；不代表每次都会擦除。 */

#endif /* STORAGE_TASK_CONFIG_H */
