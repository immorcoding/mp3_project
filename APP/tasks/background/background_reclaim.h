/**
 ******************************************************************************
 * @file    background_reclaim.h
 * @brief   Flash 回收作业：后台任务自己的内置作业，不进注册表（STOR-12、#196 第 18、19 条、#220）。
 *
 * @details
 *          Flash 卷就绪之后每 100 ms 给 FTL 一次回收机会，一次最多一块；播放和暂停时也提供机会（打开的音频流不影响，
 *          前提是每卷一把锁，且运行中没有任务读 QSPI 映射窗口）。失败一次就停：记一条 ERROR，到 Flash 卷序号变化为止。
 *          Flash 卷序号为 0（开机至今没发过卷事件，Flash 启动任务还在 `MountFlash` 或挂载失败）时不碰 Flash 卷。
 *          这是单步函数，形状同其他后台作业（`uint32_t step(void)`）；后台任务订阅卷事件，所以“等卷事件”靠通知叫醒。
 ******************************************************************************
 */

#ifndef BACKGROUND_RECLAIM_H
#define BACKGROUND_RECLAIM_H

#include <stdint.h>

/**
 * @brief  回收作业单步。
 * @return 多少毫秒后再叫它：距下一次回收机会还剩的时间；卷序号为 0、卷没挂载、已因失败停下时为 `BACKGROUND_WAIT_FOREVER`
 *         （等卷事件的通知）。
 */
uint32_t background_reclaim_step(void);

#endif /* BACKGROUND_RECLAIM_H */
