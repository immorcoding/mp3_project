/**
  ******************************************************************************
  * @file    foreground_load.h
  * @brief   前台任务在各阶段施加的负载：读大 TXT、Flash 写入与回收、ui_cell 合成拷贝（spike #191）。
  *
  * @details
  *          每个负载用 `foreground_load_step()` 做一小步（至多一次 32 KiB 读、一次 64 KiB 写或一次合成拷贝）
  *          就返回，步内自带节拍。负载只走 Filesystem Service 的公开接口，不做 CPU 重活，不做界面。
  *          耗时取 Service 内部“拿到锁之后到返回”的持锁时长（`Service_Filesystem_SpikeLastHoldUs()`），
  *          所以不含等锁。
  ******************************************************************************
  */

#ifndef FOREGROUND_LOAD_H
#define FOREGROUND_LOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "APP/tasks/foreground/foreground_stats.h"
#include "APP/ui_cell/ui_cell.h"

/** @brief 一个阶段里前台任务施加的负载种类。 */
typedef enum
{
    FOREGROUND_LOAD_NONE = 0U, /**< 无负载。 */
    FOREGROUND_LOAD_TXT,       /**< 读 SD 上的大 TXT，模拟分页。 */
    FOREGROUND_LOAD_FLASH,     /**< Flash 卷写入、同步、删除与回收。 */
    FOREGROUND_LOAD_CELL       /**< ui_cell 合成拷贝。 */
} ForegroundLoadKindTypeDef;

/** @brief 负载统计，按阶段清零。 */
typedef struct
{
    ForegroundStatTypeDef Txt32k;   /**< 32 KiB 读：拿到锁之后到返回的时间。 */
    ForegroundStatTypeDef Txt4k;    /**< 随机页 4 KiB 读。 */
    uint32_t TxtErrors;             /**< 读取失败次数。 */
    uint32_t TxtLastStatus;         /**< 最近一次失败的 Service 状态值。 */
    uint32_t TxtNewlines;           /**< 已统计的换行数。 */
    uint32_t TxtPasses;             /**< 读到文件尾回绕的次数。 */
    ForegroundStatTypeDef FlashWrite;   /**< 64 KiB 写。 */
    ForegroundStatTypeDef FlashSync;    /**< 每次写之后的同步。 */
    ForegroundStatTypeDef FlashReclaim; /**< 一次回收机会。 */
    uint32_t FlashFiles;            /**< 写满并删除的测试文件个数。 */
    uint32_t FlashErrors;           /**< Flash 负载失败次数。 */
} ForegroundLoadStatsTypeDef;

void foreground_load_init(void);
void foreground_load_stats_reset(void);
const ForegroundLoadStatsTypeDef *foreground_load_stats(void);
const UiCell *foreground_load_cell(void);
bool foreground_load_begin(ForegroundLoadKindTypeDef kind, char *detail, size_t detail_size);
void foreground_load_end(ForegroundLoadKindTypeDef kind);
void foreground_load_step(ForegroundLoadKindTypeDef kind);
void foreground_gui_step(void);
const char *foreground_status_name(uint32_t status);

#endif /* FOREGROUND_LOAD_H */
