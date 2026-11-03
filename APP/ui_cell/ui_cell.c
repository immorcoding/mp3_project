/**
  ******************************************************************************
  * @file    ui_cell.c
  * @brief   界面数据交接格子的实现：短锁、整份拷贝、序号、写后叫醒读者。
  ******************************************************************************
  */

#include "APP/ui_cell/ui_cell.h"

#include <string.h>

#include "APP/spike_time.h"

/**
 * @brief 初始化格子：建互斥量、清序号和统计。
 * @param[out] cell 要初始化的格子。
 * @param[in] storage 格子本体，调用者提供，至少 size 字节。
 * @param[in] size 本体字节数。
 * @param[in] wake_task 写后叫醒的读者任务，可为空。
 * @param[in] wake_flag 叫醒时置给 wake_task 的通知位。
 * @note  任何读写之前调用一次；互斥量静态创建，不占内核堆。
 */
void ui_cell_init(UiCell *cell, void *storage, size_t size, TaskHandle_t wake_task, uint32_t wake_flag)
{
    memset(cell, 0, sizeof(*cell));
    cell->Storage = storage;
    cell->Size = size;
    cell->WakeTask = wake_task;
    cell->WakeFlag = wake_flag;
    cell->Lock = xSemaphoreCreateMutexStatic(&cell->LockStorage);
    configASSERT(cell->Lock != NULL);
}

/**
 * @brief 在已持锁时拷入新内容、递增序号并记写入统计。
 * @param[in,out] cell 格子；调用者持有它的锁。
 * @param[in] src 至少 cell->Size 字节的新内容。
 * @param[in] start 拿到锁之后的起点时间戳。
 */
static void ui_cell_store_locked(UiCell *cell, const void *src, const spike_stamp_t *start)
{
    uint32_t hold_us;

    memcpy(cell->Storage, src, cell->Size);
    cell->Sequence++;
    hold_us = spike_stamp_elapsed_us(start);
    cell->Stats.WriteCount++;
    cell->Stats.WriteSumUs += hold_us;
    if (hold_us > cell->Stats.WriteMaxUs)
    {
        cell->Stats.WriteMaxUs = hold_us;
    }
}

/**
 * @brief 写后叫醒读者任务（如果登记了）。
 * @param[in] cell 刚写过的格子。
 */
static void ui_cell_wake_reader(const UiCell *cell)
{
    if (cell->WakeTask != NULL)
    {
        (void)xTaskNotify(cell->WakeTask, cell->WakeFlag, eSetBits);
    }
}

/**
 * @brief 写入整份内容，等到拿到锁为止（前台、后台任务用）。
 * @param[in,out] cell 格子。
 * @param[in] src 至少 cell->Size 字节的新内容。
 * @note  持锁期间只拷贝内存。同一个格子只能有一个写者任务。
 */
void ui_cell_write(UiCell *cell, const void *src)
{
    spike_stamp_t start;

    while (xSemaphoreTake(cell->Lock, portMAX_DELAY) != pdTRUE)
    {
    }
    start = spike_stamp_now();
    ui_cell_store_locked(cell, src, &start);
    (void)xSemaphoreGive(cell->Lock);
    ui_cell_wake_reader(cell);
}

/**
 * @brief 尝试写入整份内容，拿不到锁就放弃（GUI 用）。
 * @param[in,out] cell 格子。
 * @param[in] src 至少 cell->Size 字节的新内容。
 * @return true 已写入；false 锁被占用，格子内容与序号都没有变。
 */
bool ui_cell_try_write(UiCell *cell, const void *src)
{
    spike_stamp_t start;

    if (xSemaphoreTake(cell->Lock, 0U) != pdTRUE)
    {
        cell->Stats.TryWriteFails++;
        return false;
    }

    start = spike_stamp_now();
    ui_cell_store_locked(cell, src, &start);
    (void)xSemaphoreGive(cell->Lock);
    ui_cell_wake_reader(cell);
    return true;
}

/**
 * @brief 序号变了才拿锁把整份内容拷到 dst，永不等待。
 * @param[in,out] cell 格子。
 * @param[out] dst 至少 cell->Size 字节的读者副本。
 * @param[in,out] seen 读者持有的已见序号；成功拷贝后更新。
 * @return true 拷贝了新内容；false 没有变化，或锁被占用（dst 与 seen 都不变，下一帧再试）。
 * @note  先不拿锁比较序号，所以没有变化的帧不碰锁。
 */
bool ui_cell_read_if_changed(UiCell *cell, void *dst, uint32_t *seen)
{
    spike_stamp_t start;
    uint32_t sequence;
    uint32_t hold_us;

    if (cell->Sequence == *seen)
    {
        return false;
    }

    cell->Stats.ReadAttempts++;
    if (xSemaphoreTake(cell->Lock, 0U) != pdTRUE)
    {
        cell->Stats.ReadLockFails++;
        return false;
    }

    start = spike_stamp_now();
    memcpy(dst, cell->Storage, cell->Size);
    sequence = cell->Sequence;
    hold_us = spike_stamp_elapsed_us(&start);
    (void)xSemaphoreGive(cell->Lock);

    *seen = sequence;
    cell->Stats.ReadCount++;
    cell->Stats.ReadSumUs += hold_us;
    if (hold_us > cell->Stats.ReadMaxUs)
    {
        cell->Stats.ReadMaxUs = hold_us;
    }
    return true;
}

/**
 * @brief 拷出统计快照。
 * @param[in] cell 格子。
 * @param[out] stats 接收快照；各字段单独读取，彼此不保证同一时刻。
 */
void ui_cell_stats_get(const UiCell *cell, UiCellStatsTypeDef *stats)
{
    stats->WriteCount = cell->Stats.WriteCount;
    stats->WriteSumUs = cell->Stats.WriteSumUs;
    stats->WriteMaxUs = cell->Stats.WriteMaxUs;
    stats->TryWriteFails = cell->Stats.TryWriteFails;
    stats->ReadCount = cell->Stats.ReadCount;
    stats->ReadSumUs = cell->Stats.ReadSumUs;
    stats->ReadMaxUs = cell->Stats.ReadMaxUs;
    stats->ReadAttempts = cell->Stats.ReadAttempts;
    stats->ReadLockFails = cell->Stats.ReadLockFails;
}

/**
 * @brief 清零统计（序号与内容不变）。
 * @param[in,out] cell 格子。
 * @note  阶段开始时由前台任务调用；清零与写者、读者的更新不同步，可能丢一次更新，spike 可接受。
 */
void ui_cell_stats_reset(UiCell *cell)
{
    cell->Stats.WriteCount = 0U;
    cell->Stats.WriteSumUs = 0U;
    cell->Stats.WriteMaxUs = 0U;
    cell->Stats.TryWriteFails = 0U;
    cell->Stats.ReadCount = 0U;
    cell->Stats.ReadSumUs = 0U;
    cell->Stats.ReadMaxUs = 0U;
    cell->Stats.ReadAttempts = 0U;
    cell->Stats.ReadLockFails = 0U;
}
