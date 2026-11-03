/**
 * @file filesystem_spike.h
 * @brief Filesystem 锁的观测接口：仅供可丢弃分支 spike/app-tasks 的前台任务读取（#191）。
 *
 * @details
 *          不属于产品公开 Interface，也不会合入。统计量是每把锁的最长持锁与最长等锁（各带任务名和
 *          操作名）、每个任务最近一次公开调用的持锁时长（读取即清零），以及 GUI/播放任务误调文件接口的次数。
 *          耗时用 DWT 周期计数器量，同时记 tick 兜底（DWT 为 32 位，480 MHz 约 8.9 s 回绕，
 *          超过 4 s 的长操作改按 tick 取整到毫秒）。
 */

#ifndef FILESYSTEM_SPIKE_H
#define FILESYSTEM_SPIKE_H

#include <stdbool.h>
#include <stdint.h>

#define SERVICE_FILESYSTEM_SPIKE_LOCK_MAX 3U     /* 全局一把时只有 1 把；每卷一把时为 SD、Flash、表锁共 3 把。 */
#define SERVICE_FILESYSTEM_SPIKE_NAME_BYTES 16U  /* 任务名缓冲，含结尾 NUL，与 configMAX_TASK_NAME_LEN 一致。 */

/** @brief 一把锁在一个统计范围（阶段窗口或自启动）内的极值。 */
typedef struct
{
    uint32_t HoldMaxUs;                                           /**< 单次持锁最长，微秒。 */
    char HoldTask[SERVICE_FILESYSTEM_SPIKE_NAME_BYTES];           /**< 该次持锁的任务名。 */
    const char *HoldOperation;                                    /**< 该次持锁的公开操作名（静态字符串）。 */
    uint32_t WaitMaxUs;                                           /**< 单次等锁最长，微秒。 */
    char WaitTask[SERVICE_FILESYSTEM_SPIKE_NAME_BYTES];           /**< 该次等锁的任务名。 */
    const char *WaitOperation;                                    /**< 该次等锁的公开操作名。 */
    char BlockerTask[SERVICE_FILESYSTEM_SPIKE_NAME_BYTES];        /**< 该次等锁开始时的持锁者任务名（尽力而为，可能为空）。 */
    const char *BlockerOperation;                                 /**< 该次等锁开始时的持锁者操作名（尽力而为，可能为空）。 */
    uint32_t AcquireCount;                                        /**< 拿锁次数。 */
} Service_Filesystem_SpikeLockStatsTypeDef;

void Service_Filesystem_SpikeInit(void);
bool Service_Filesystem_SpikePerVolume(void);
uint32_t Service_Filesystem_SpikeLockCount(void);
const char *Service_Filesystem_SpikeLockName(uint32_t index);
bool Service_Filesystem_SpikeGetLockStats(uint32_t index,
                                          Service_Filesystem_SpikeLockStatsTypeDef *window,
                                          Service_Filesystem_SpikeLockStatsTypeDef *total);
void Service_Filesystem_SpikeWindowReset(void);
uint32_t Service_Filesystem_SpikeLastHoldUs(void);
uint32_t Service_Filesystem_SpikeForbiddenCalls(void);

#endif /* FILESYSTEM_SPIKE_H */
