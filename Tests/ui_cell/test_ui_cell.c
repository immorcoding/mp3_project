/**
 ******************************************************************************
 * @file    test_ui_cell.c
 * @brief   界面数据交接格子 ui_cell 的主机行为测试（#193 草案的测试表，spike #191）。
 *
 * @details
 *          主机是单线程的：用“自己先把格子里的锁拿住”模拟另一个任务正在持锁。真实并发下的持锁时长、
 *          GUI 拿不到锁的频率和体感只能上板。
 ******************************************************************************
 */

#include "APP/ui_cell/ui_cell.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

unsigned host_lock_stub_notify_count(void);
uint32_t host_lock_stub_notify_value(void);

#define CELL_BYTES 64U

/**
 * @brief 生成一份可辨认的内容。
 * @param[out] buffer CELL_BYTES 字节。
 * @param[in] seed 种子，不同种子内容不同。
 */
static void fill_pattern(uint8_t *buffer, uint8_t seed)
{
    for (uint32_t index = 0U; index < CELL_BYTES; index++)
    {
        buffer[index] = (uint8_t)(seed + index);
    }
}

/**
 * @brief 写入后读者拿到整份内容、序号随之前进，读到之后不再有变化。
 */
static void test_whole_copy_and_sequence(void)
{
    UiCell cell;
    uint8_t storage[CELL_BYTES];
    uint8_t source[CELL_BYTES];
    uint8_t copy[CELL_BYTES] = {0};
    uint32_t seen = 0U;

    ui_cell_init(&cell, storage, sizeof(storage), NULL, 0U);
    fill_pattern(source, 7U);

    assert(!ui_cell_read_if_changed(&cell, copy, &seen));
    ui_cell_write(&cell, source);
    assert(cell.Sequence == 1U);
    assert(ui_cell_read_if_changed(&cell, copy, &seen));
    assert(memcmp(copy, source, CELL_BYTES) == 0);
    assert(seen == 1U);

    /* 没有新写入：序号没变，不拷贝。 */
    memset(copy, 0, sizeof(copy));
    assert(!ui_cell_read_if_changed(&cell, copy, &seen));
    assert(copy[0] == 0U);

    fill_pattern(source, 99U);
    ui_cell_write(&cell, source);
    ui_cell_write(&cell, source);
    assert(cell.Sequence == 3U);
    assert(ui_cell_read_if_changed(&cell, copy, &seen));
    assert(memcmp(copy, source, CELL_BYTES) == 0);
    assert(seen == 3U);
}

/**
 * @brief 序号没变的帧不碰锁：即使锁被别的任务占着，也不记尝试、不记失败。
 */
static void test_unchanged_frame_does_not_touch_the_lock(void)
{
    UiCell cell;
    uint8_t storage[CELL_BYTES];
    uint8_t copy[CELL_BYTES] = {0};
    uint32_t seen = 0U;

    ui_cell_init(&cell, storage, sizeof(storage), NULL, 0U);
    assert(xSemaphoreTake(cell.Lock, portMAX_DELAY) == pdTRUE);

    assert(!ui_cell_read_if_changed(&cell, copy, &seen));
    assert(cell.Stats.ReadAttempts == 0U);
    assert(cell.Stats.ReadLockFails == 0U);

    assert(xSemaphoreGive(cell.Lock) == pdTRUE);
}

/**
 * @brief 拿不到锁时：try 版本返回假且数据与序号不变，读者保持原样、记一次失败，下一帧再试能成功。
 */
static void test_busy_lock_leaves_data_unchanged(void)
{
    UiCell cell;
    uint8_t storage[CELL_BYTES];
    uint8_t source[CELL_BYTES];
    uint8_t newer[CELL_BYTES];
    uint8_t copy[CELL_BYTES] = {0};
    uint32_t seen = 0U;

    ui_cell_init(&cell, storage, sizeof(storage), NULL, 0U);
    fill_pattern(source, 1U);
    fill_pattern(newer, 50U);
    ui_cell_write(&cell, source);

    assert(xSemaphoreTake(cell.Lock, portMAX_DELAY) == pdTRUE);

    assert(!ui_cell_try_write(&cell, newer));
    assert(cell.Stats.TryWriteFails == 1U);
    assert(cell.Sequence == 1U);
    assert(memcmp(storage, source, CELL_BYTES) == 0);

    assert(!ui_cell_read_if_changed(&cell, copy, &seen));
    assert(cell.Stats.ReadAttempts == 1U);
    assert(cell.Stats.ReadLockFails == 1U);
    assert(seen == 0U);
    assert(copy[0] == 0U);

    assert(xSemaphoreGive(cell.Lock) == pdTRUE);

    assert(ui_cell_read_if_changed(&cell, copy, &seen));
    assert(memcmp(copy, source, CELL_BYTES) == 0);
    assert(ui_cell_try_write(&cell, newer));
    assert(cell.Sequence == 2U);
    assert(memcmp(storage, newer, CELL_BYTES) == 0);
}

/**
 * @brief 写后叫醒登记的读者任务，未登记则不叫醒。
 */
static void test_write_wakes_registered_reader(void)
{
    UiCell cell;
    uint8_t storage[CELL_BYTES];
    uint8_t source[CELL_BYTES];
    int fake_reader = 0;
    const unsigned before = host_lock_stub_notify_count();

    fill_pattern(source, 3U);

    ui_cell_init(&cell, storage, sizeof(storage), NULL, 0U);
    ui_cell_write(&cell, source);
    assert(host_lock_stub_notify_count() == before);

    ui_cell_init(&cell, storage, sizeof(storage), &fake_reader, 0x10U);
    ui_cell_write(&cell, source);
    assert(host_lock_stub_notify_count() == before + 1U);
    assert(host_lock_stub_notify_value() == 0x10U);
    assert(ui_cell_try_write(&cell, source));
    assert(host_lock_stub_notify_count() == before + 2U);
}

/**
 * @brief 统计记次数、总和与最大值，重置后清零但不动序号与内容。
 */
static void test_stats_count_and_reset(void)
{
    UiCell cell;
    UiCellStatsTypeDef stats;
    uint8_t storage[CELL_BYTES];
    uint8_t source[CELL_BYTES];
    uint8_t copy[CELL_BYTES];
    uint32_t seen = 0U;

    ui_cell_init(&cell, storage, sizeof(storage), NULL, 0U);
    fill_pattern(source, 9U);
    ui_cell_write(&cell, source);
    ui_cell_write(&cell, source);
    assert(ui_cell_read_if_changed(&cell, copy, &seen));

    ui_cell_stats_get(&cell, &stats);
    assert(stats.WriteCount == 2U);
    assert(stats.WriteMaxUs >= 1U);
    assert(stats.WriteSumUs >= stats.WriteMaxUs);
    assert(stats.ReadCount == 1U);
    assert(stats.ReadAttempts == 1U);
    assert(stats.ReadMaxUs >= 1U);

    ui_cell_stats_reset(&cell);
    ui_cell_stats_get(&cell, &stats);
    assert(stats.WriteCount == 0U);
    assert(stats.ReadCount == 0U);
    assert(stats.ReadMaxUs == 0U);
    assert(cell.Sequence == 2U);
    assert(memcmp(storage, source, CELL_BYTES) == 0);
}

/**
 * @brief 运行全部用例。
 * @return 全部断言通过时返回 0。
 */
int main(void)
{
    test_whole_copy_and_sequence();
    test_unchanged_frame_does_not_touch_the_lock();
    test_busy_lock_leaves_data_unchanged();
    test_write_wakes_registered_reader();
    test_stats_count_and_reset();
    printf("ui_cell tests passed\n");
    return 0;
}
