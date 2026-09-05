/**
  ******************************************************************************
  * @file    storage_task_config.h
  * @brief   Storage Task 的私有热插拔调度参数。
  ******************************************************************************
  */

#ifndef STORAGE_TASK_CONFIG_H
#define STORAGE_TASK_CONFIG_H

/* storage_task.h */
#define STORAGE_FLASH_AUTO_FORMAT        1U    /* MountFlash 得到无文件系统时是否 FormatAndMountFlash。 */

#define STORAGE_NOTIFY_FLAG_SD_DETECT    (1UL << 0) /* 主循环事件槽：卡检测边沿。 */
#define STORAGE_NOTIFY_FLAG_LISTBUFFER   (1UL << 1) /* 主循环事件槽：Queue 窗口 PENDING。 */
#define STORAGE_NOTIFY_FLAG_EVENT_MASK   (STORAGE_NOTIFY_FLAG_SD_DETECT | \
                                          STORAGE_NOTIFY_FLAG_LISTBUFFER) /* 主循环一次收取并清除的事件位。 */

/* storage_task.c */
#define STORAGE_SD_DEBOUNCE_MS           30U   /* SD 卡检测输入在最后一个边沿后的最短稳定时间，单位为毫秒。 */
#define STORAGE_FLASH_RECLAIM_PERIOD_MS  100U  /* 空闲时回收机会周期；不代表每次都会擦除。 */

#endif /* STORAGE_TASK_CONFIG_H */
