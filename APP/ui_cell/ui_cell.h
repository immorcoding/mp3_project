/**
  ******************************************************************************
  * @file    ui_cell.h
  * @brief   界面数据与最新需求的交接格子（#193 草案的 spike 实现，可丢弃分支）。
  *
  * @details
  *          GUI 与任务之间的小模块：一个格子带一把藏在模块内的互斥锁和一个序号，只传整份内存。
  *          模块不对外给锁，持锁时只拷贝内存，所以“持有界面数据锁时调用文件接口”写不出来。
  *          先不拿锁比较序号，变了才拿锁拷贝，没有变化的帧不碰锁。写者等到拿到锁（前台、后台任务）
  *          或不等（GUI 的 try 版本）；读者永远不等，拿不到就保持原样、下一帧再试。
  *          一个格子只有一个写者，可以有多个读者。模块不认识应用，也不认识内容。
  *
  *          本分支只用它跑一个合成场景（前台任务每 16 ms 写 6 KiB，GUI 每圈读），结果仅作参考，
  *          音乐不走它。持锁时长按 spike_time.h 的方式量（DWT 加 tick 兜底），读写各记次数、总和与最大值。
  ******************************************************************************
  */

#ifndef UI_CELL_H
#define UI_CELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 一个格子的持锁与争用统计；写者字段只有写者任务写，读者字段只有读者任务写。 */
typedef struct
{
    uint32_t WriteCount;      /**< 成功写入次数。 */
    uint32_t WriteSumUs;      /**< 写入持锁时长总和，微秒。 */
    uint32_t WriteMaxUs;      /**< 写入持锁时长最大值，微秒。 */
    uint32_t TryWriteFails;   /**< `ui_cell_try_write` 拿不到锁的次数。 */
    uint32_t ReadCount;       /**< 成功拷贝次数。 */
    uint32_t ReadSumUs;       /**< 读取持锁时长总和，微秒。 */
    uint32_t ReadMaxUs;       /**< 读取持锁时长最大值，微秒。 */
    uint32_t ReadAttempts;    /**< 序号变了、尝试拿锁拷贝的次数。 */
    uint32_t ReadLockFails;   /**< 其中拿不到锁的次数。 */
} UiCellStatsTypeDef;

/** @brief 交接格子；字段仅供 ui_cell.c 使用，调用者静态持有即可。 */
typedef struct
{
    void *Storage;                    /**< 格子本体，由调用者提供，大小固定。 */
    size_t Size;                      /**< 本体字节数。 */
    volatile uint32_t Sequence;       /**< 每次写入递增；读者无锁比较。 */
    SemaphoreHandle_t Lock;           /**< 模块内的互斥量，不对外提供。 */
    StaticSemaphore_t LockStorage;    /**< 互斥量的静态存储。 */
    TaskHandle_t WakeTask;            /**< 写后叫醒的读者任务；为空不叫醒。 */
    uint32_t WakeFlag;                /**< 叫醒时置给 WakeTask 的通知位。 */
    volatile UiCellStatsTypeDef Stats;/**< 统计。 */
} UiCell;

void ui_cell_init(UiCell *cell, void *storage, size_t size, TaskHandle_t wake_task, uint32_t wake_flag);
void ui_cell_write(UiCell *cell, const void *src);
bool ui_cell_try_write(UiCell *cell, const void *src);
bool ui_cell_read_if_changed(UiCell *cell, void *dst, uint32_t *seen);
void ui_cell_stats_get(const UiCell *cell, UiCellStatsTypeDef *stats);
void ui_cell_stats_reset(UiCell *cell);

#endif /* UI_CELL_H */
