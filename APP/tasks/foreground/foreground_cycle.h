/**
  ******************************************************************************
  * @file    foreground_cycle.h
  * @brief   表锁最坏延迟的负载（spike #199）：Flash 上的大文件写入与删除循环，以及大目录卡上按路径打开最后一首。
  *
  * @details
  *          Flash 循环：新建文件、写满目标字节、同步、关闭、改名、删除，每一步单独记墙钟耗时（含等锁）；
  *          删除另记卷锁持有。表锁最长持有按操作分开，由 Filesystem 的锁统计给出，这里不重复。
  *          按路径打开：先扫一遍大目录找到最后一个文件，再重复打开、关闭它，记墙钟与 SD 卷锁持有。
  *          两种负载只走 Filesystem Service 的公开接口。
  ******************************************************************************
  */

#ifndef FOREGROUND_CYCLE_H
#define FOREGROUND_CYCLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "APP/tasks/foreground/foreground_stats.h"
#include "Service/filesystem/filesystem_spike.h"

/** @brief Flash 循环的统计，按阶段清零。 */
typedef struct
{
    uint32_t TargetBytes;                 /**< 每个文件的目标字节数。 */
    uint32_t Cycles;                      /**< 完整走完（含删除）的循环数。 */
    uint32_t PartialCycles;               /**< 没写满目标（空间不足或到写入时限）就收尾的循环数。 */
    uint32_t Errors;                      /**< 出错次数。 */
    uint32_t LastBytes;                   /**< 最近一次删除的文件字节数。 */
    ForegroundStatTypeDef Create;         /**< 新建文件（CREATE_NEW 打开）。 */
    ForegroundStatTypeDef WriteChunk;     /**< 一次 64 KiB 写。 */
    ForegroundStatTypeDef WriteTotal;     /**< 一个文件从第一次写到最后一次写的总耗时。 */
    ForegroundStatTypeDef Sync;           /**< 同步。 */
    ForegroundStatTypeDef Close;          /**< 关闭。 */
    ForegroundStatTypeDef Rename;         /**< 改名。 */
    ForegroundStatTypeDef Delete;         /**< 删除的墙钟耗时（含等 Flash 卷锁）。 */
    ForegroundStatTypeDef DeleteHold;     /**< 删除持有 Flash 卷锁的时长。 */
    bool BlocksValid;                     /**< 块计数读取成功。 */
    Service_Filesystem_SpikeFlashBlocksTypeDef BlocksStart; /**< 阶段开始时的块计数。 */
    Service_Filesystem_SpikeFlashBlocksTypeDef BlocksNow;   /**< 汇总时重新读的块计数。 */
    uint32_t GcLow;                       /**< 回收启动水位（按 FTL 公式推算）。 */
} ForegroundCycleStatsTypeDef;

/** @brief 按路径打开最后一首的统计，按阶段清零。 */
typedef struct
{
    bool Found;                           /**< 找到了最后一个文件。 */
    uint32_t Entries;                     /**< 目录里的文件数（不含子目录）。 */
    uint32_t ScanUs;                      /**< 扫完整个目录的墙钟耗时。 */
    uint32_t Errors;                      /**< 出错次数。 */
    uint32_t LastStatus;                  /**< 最近一次出错的 Service 状态值。 */
    ForegroundStatTypeDef Open;           /**< 按路径打开的墙钟耗时（含等锁）。 */
    ForegroundStatTypeDef OpenHold;       /**< 按路径打开持有 SD 卷锁的时长。 */
    ForegroundStatTypeDef Close;          /**< 关闭。 */
    char Last[48];                        /**< 最后一个文件名，过长则截断，只用于日志。 */
} ForegroundOpenLastStatsTypeDef;

void foreground_cycle_reset(void);
bool foreground_cycle_begin(uint32_t target_bytes, uint32_t write_cap_ms, char *detail, size_t detail_size);
void foreground_cycle_end(void);
void foreground_cycle_step(void);
bool foreground_cycle_finished(void);
void foreground_cycle_refresh(void);
const ForegroundCycleStatsTypeDef *foreground_cycle_stats(void);

void foreground_openlast_reset(void);
bool foreground_openlast_begin(char *detail, size_t detail_size);
void foreground_openlast_end(void);
void foreground_openlast_step(void);
bool foreground_openlast_finished(void);
const ForegroundOpenLastStatsTypeDef *foreground_openlast_stats(void);

#endif /* FOREGROUND_CYCLE_H */
