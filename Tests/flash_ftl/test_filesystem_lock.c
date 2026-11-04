/**
 * @file test_filesystem_lock.c
 * @brief Filesystem 私有锁的主机行为测试（spike #191、#199）：统计、窗口、按任务的最近持锁、误调计数、
 *        按操作分开的最长持有、槽表锁与宽窄表锁的范围。
 *
 * @details
 *          同一份源码编译三次：默认全局一把锁、`SPIKE_LOCK_PER_VOLUME=1` 的每卷一把加宽表锁，
 *          以及再加 `SPIKE_TABLE_LOCK_NARROW=1` 的窄表锁。
 *          主机是单线程的，这里只验证锁模块的记账和“公开函数不会重复拿同一把锁”的前提
 *          （互斥量替身在重复获取时会中止进程）；真实并发、优先级继承和等锁时长只能上板。
 */

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/filesystem/filesystem_lock.h"
#include "Service/filesystem/filesystem_spike.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void host_lock_stub_set_task_name(const char *name);
unsigned host_lock_stub_log_count(void);

/**
 * @brief 让周期计数器替身多走几步，制造更长的持锁时间。
 * @param[in] steps 读取次数，每次前进 1 µs。
 */
static void burn_microseconds(unsigned steps)
{
    while (steps-- > 0U)
    {
        (void)CortexM7CycleCounter_Read();
    }
}

/**
 * @brief 一次拿放被记成一次拿锁，持锁时长为正，极值带任务名和操作名。
 */
static void test_acquire_release_records_hold_and_names(void)
{
    Service_Filesystem_SpikeLockStatsTypeDef window;
    Service_Filesystem_SpikeLockStatsTypeDef total;

    filesystem_lock_init();
    Service_Filesystem_SpikeWindowReset();

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "ReadFile");
    burn_microseconds(10U);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);

    assert(Service_Filesystem_SpikeGetLockStats(0U, &window, &total));
    assert(window.AcquireCount == 1U);
    assert(total.AcquireCount >= 1U);
    assert(window.HoldMaxUs >= 10U);
    assert(strcmp(window.HoldOperation, "ReadFile") == 0);
    assert(strcmp(window.HoldTask, "host") == 0);
    assert(window.WaitMaxUs >= 1U);
    assert(strcmp(window.WaitOperation, "ReadFile") == 0);
    assert(strcmp(window.WaitTask, "host") == 0);
    assert(Service_Filesystem_SpikeLastHoldUs() == window.HoldMaxUs);
}

/**
 * @brief 最长持锁保留较长的那次，较短的后一次不覆盖它的操作名。
 */
static void test_longest_hold_keeps_its_operation(void)
{
    Service_Filesystem_SpikeLockStatsTypeDef window;

    Service_Filesystem_SpikeWindowReset();

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "ReadFile");
    burn_microseconds(50U);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "SeekFile");
    burn_microseconds(2U);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);

    assert(Service_Filesystem_SpikeGetLockStats(0U, &window, NULL));
    assert(window.AcquireCount == 2U);
    assert(window.HoldMaxUs >= 50U);
    assert(strcmp(window.HoldOperation, "ReadFile") == 0);
    /* 任务最近一次持锁时长是后一次（较短）的，不是极值；读取即清零，没拿到锁的调用不会读到旧值。 */
    assert(Service_Filesystem_SpikeLastHoldUs() < window.HoldMaxUs);
    assert(Service_Filesystem_SpikeLastHoldUs() == 0U);
}

/**
 * @brief 窗口重置只清窗口统计，自启动统计保留。
 */
static void test_window_reset_keeps_total(void)
{
    Service_Filesystem_SpikeLockStatsTypeDef window;
    Service_Filesystem_SpikeLockStatsTypeDef total;

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "ReadFile");
    burn_microseconds(5U);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    assert(Service_Filesystem_SpikeGetLockStats(0U, NULL, &total));
    assert(total.HoldMaxUs >= 50U);

    Service_Filesystem_SpikeWindowReset();
    assert(Service_Filesystem_SpikeGetLockStats(0U, &window, &total));
    assert(window.AcquireCount == 0U);
    assert(window.HoldMaxUs == 0U);
    assert(window.HoldOperation == NULL);
    assert(total.HoldMaxUs >= 50U);
}

/**
 * @brief GUI 或播放任务调用文件接口：计数，只记一次 WARN，不停机。
 */
static void test_forbidden_caller_is_counted_and_logged_once(void)
{
    const unsigned logs_before = host_lock_stub_log_count();
    const uint32_t forbidden_before = Service_Filesystem_SpikeForbiddenCalls();

    host_lock_stub_set_task_name("GUI Task");
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "OpenFile");
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    host_lock_stub_set_task_name("Playback Task");
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "ReadFile");
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    host_lock_stub_set_task_name("host");

    assert(Service_Filesystem_SpikeForbiddenCalls() == forbidden_before + 2U);
    assert(host_lock_stub_log_count() == logs_before + 1U);
}

/**
 * @brief 锁个数与名字随编译模式变化；每卷一把时两把卷锁互相独立，表锁嵌套在卷锁内，槽表锁在最内层。
 */
static void test_lock_layout_matches_build_mode(void)
{
#if FILESYSTEM_LOCK_PER_VOLUME
    Service_Filesystem_SpikeLockStatsTypeDef sd_window;
    Service_Filesystem_SpikeLockStatsTypeDef flash_window;
    Service_Filesystem_SpikeLockStatsTypeDef table_window;
    Service_Filesystem_SpikeLockStatsTypeDef slot_window;

    assert(Service_Filesystem_SpikePerVolume());
    assert(Service_Filesystem_SpikeLockCount() == 4U);
    assert(strcmp(Service_Filesystem_SpikeLockName(0U), "SD") == 0);
    assert(strcmp(Service_Filesystem_SpikeLockName(1U), "FLASH") == 0);
    assert(strcmp(Service_Filesystem_SpikeLockName(2U), "TABLE") == 0);
    assert(strcmp(Service_Filesystem_SpikeLockName(3U), "SLOT") == 0);

    Service_Filesystem_SpikeWindowReset();
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "ReadFile");
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "WriteFile");
    filesystem_lock_table_acquire("OpenFile");
    filesystem_lock_slot_acquire("SlotAcquire");
    filesystem_lock_slot_release();
    filesystem_lock_table_release();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);

    assert(Service_Filesystem_SpikeGetLockStats(0U, &sd_window, NULL));
    assert(Service_Filesystem_SpikeGetLockStats(1U, &flash_window, NULL));
    assert(Service_Filesystem_SpikeGetLockStats(2U, &table_window, NULL));
    assert(Service_Filesystem_SpikeGetLockStats(3U, &slot_window, NULL));
    assert(sd_window.AcquireCount == 1U);
    assert(flash_window.AcquireCount == 1U);
    assert(table_window.AcquireCount == 1U);
    assert(slot_window.AcquireCount == 1U);
    assert(strcmp(sd_window.HoldOperation, "ReadFile") == 0);
    assert(strcmp(flash_window.HoldOperation, "WriteFile") == 0);
    assert(strcmp(table_window.HoldOperation, "OpenFile") == 0);
    assert(strcmp(slot_window.HoldOperation, "SlotAcquire") == 0);
#else
    assert(!Service_Filesystem_SpikePerVolume());
    assert(Service_Filesystem_SpikeLockCount() == 1U);
    assert(strcmp(Service_Filesystem_SpikeLockName(0U), "GLOBAL") == 0);
    assert(!Service_Filesystem_SpikeGetLockStats(1U, NULL, NULL));

    /* 全局一把：两个卷共用同一把锁，表锁是空操作，所以先后拿放不会重复获取。 */
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "WriteFile");
    filesystem_lock_table_acquire("OpenFile");
    filesystem_lock_slot_acquire("SlotAcquire");
    filesystem_lock_slot_release();
    filesystem_lock_table_release();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "ReadFile");
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
#endif
}

/**
 * @brief 在一张按操作分开的窗口表里按名字找一项。
 * @param[in] table 窗口表。
 * @param[in] operation 操作名。
 * @return 条目指针；没有时为 NULL。
 */
static const Service_Filesystem_SpikeOpStatsTypeDef *find_operation(const Service_Filesystem_SpikeOpStatsTypeDef *table,
                                                                   const char *operation)
{
    uint32_t index;

    for (index = 0U; index < SERVICE_FILESYSTEM_SPIKE_OP_MAX; index++)
    {
        if ((table[index].Operation != NULL) && (strcmp(table[index].Operation, operation) == 0))
        {
            return &table[index];
        }
    }
    return NULL;
}

/**
 * @brief 同一把锁上不同操作的最长持有分开记；同名操作（不同字面量地址）合并；窗口重置只清窗口。
 */
static void test_operation_stats_are_split_by_operation(void)
{
    Service_Filesystem_SpikeOpStatsTypeDef window[SERVICE_FILESYSTEM_SPIKE_OP_MAX];
    Service_Filesystem_SpikeOpStatsTypeDef total[SERVICE_FILESYSTEM_SPIKE_OP_MAX];
    char same_name_other_address[] = "ReadFile";
    const Service_Filesystem_SpikeOpStatsTypeDef *read;
    const Service_Filesystem_SpikeOpStatsTypeDef *remove_file;

    Service_Filesystem_SpikeWindowReset();

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "ReadFile");
    burn_microseconds(10U);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "RemoveFile");
    burn_microseconds(200U);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, same_name_other_address);
    burn_microseconds(30U);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);

    assert(Service_Filesystem_SpikeGetOpStats(0U, window, total) == 2U);
    read = find_operation(window, "ReadFile");
    remove_file = find_operation(window, "RemoveFile");
    assert(read != NULL);
    assert(remove_file != NULL);
    assert(read->Count == 2U);
    assert(read->HoldMaxUs >= 30U);
    assert(read->HoldMaxUs < remove_file->HoldMaxUs);
    assert(remove_file->Count == 1U);
    assert(remove_file->HoldMaxUs >= 200U);

    Service_Filesystem_SpikeWindowReset();
    assert(Service_Filesystem_SpikeGetOpStats(0U, window, total) == 0U);
    assert(find_operation(window, "RemoveFile") == NULL);
    assert(find_operation(total, "RemoveFile") != NULL);
    assert(find_operation(total, "RemoveFile")->HoldMaxUs >= 200U);
    assert(Service_Filesystem_SpikeGetOpStats(Service_Filesystem_SpikeLockCount(), window, total) == 0U);
}

/**
 * @brief 操作种类超过表的容量时，多出来的并入最后一项“(other)”，不丢次数也不越界。
 */
static void test_operation_table_overflow_goes_to_other(void)
{
    Service_Filesystem_SpikeOpStatsTypeDef window[SERVICE_FILESYSTEM_SPIKE_OP_MAX];
    static char names[SERVICE_FILESYSTEM_SPIKE_OP_MAX + 4U][8];
    uint32_t index;
    uint32_t counted = 0U;

    Service_Filesystem_SpikeWindowReset();
    for (index = 0U; index < (SERVICE_FILESYSTEM_SPIKE_OP_MAX + 4U); index++)
    {
        (void)snprintf(names[index], sizeof(names[index]), "op%02u", (unsigned)index);
        filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, names[index]);
        filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
    }

    assert(Service_Filesystem_SpikeGetOpStats(0U, window, NULL) == SERVICE_FILESYSTEM_SPIKE_OP_MAX);
    assert(strcmp(window[SERVICE_FILESYSTEM_SPIKE_OP_MAX - 1U].Operation, "(other)") == 0);
    for (index = 0U; index < SERVICE_FILESYSTEM_SPIKE_OP_MAX; index++)
    {
        counted += window[index].Count;
    }
    assert(counted == (SERVICE_FILESYSTEM_SPIKE_OP_MAX + 4U));
    assert(window[SERVICE_FILESYSTEM_SPIKE_OP_MAX - 1U].Count == 5U);
}

/**
 * @brief 宽表锁把只写本卷条目的调用（f_close、f_unlink 等）放进表锁，窄表锁和全局一把不放。
 */
static void test_table_wide_macros_follow_build_mode(void)
{
#if FILESYSTEM_LOCK_PER_VOLUME
    Service_Filesystem_SpikeLockStatsTypeDef table_window;
    uint32_t expected = 0U;

    assert(Service_Filesystem_SpikeTableNarrow() == (FILESYSTEM_LOCK_TABLE_NARROW != 0));
    Service_Filesystem_SpikeWindowReset();
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "RemoveFile");
    filesystem_lock_table_wide_acquire("RemoveFile");
    filesystem_lock_table_wide_release();
    filesystem_lock_table_acquire("OpenFile");
    filesystem_lock_table_release();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);

    expected = FILESYSTEM_LOCK_TABLE_NARROW ? 1U : 2U;
    assert(Service_Filesystem_SpikeGetLockStats(2U, &table_window, NULL));
    assert(table_window.AcquireCount == expected);
#else
    assert(!Service_Filesystem_SpikeTableNarrow());
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "RemoveFile");
    filesystem_lock_table_wide_acquire("RemoveFile");
    filesystem_lock_table_wide_release();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
#endif
}

/**
 * @brief 只有卷锁记“最近一次公开调用的持锁时长”：表锁、槽表锁在卷锁之后放也不会盖掉它。
 */
static void test_last_hold_ignores_inner_locks(void)
{
    uint32_t volume_hold;

    Service_Filesystem_SpikeWindowReset();
    (void)Service_Filesystem_SpikeLastHoldUs();

    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "CloseFile");
    burn_microseconds(80U);
    filesystem_lock_table_acquire("CloseFile");
    filesystem_lock_table_release();
    filesystem_lock_slot_acquire("SlotRelease");
    filesystem_lock_slot_release();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);

    volume_hold = Service_Filesystem_SpikeLastHoldUs();
    assert(volume_hold >= 80U);
}

/**
 * @brief 运行全部用例。
 * @return 全部断言通过时返回 0。
 */
int main(void)
{
    test_acquire_release_records_hold_and_names();
    test_longest_hold_keeps_its_operation();
    test_window_reset_keeps_total();
    test_forbidden_caller_is_counted_and_logged_once();
    test_lock_layout_matches_build_mode();
    test_operation_stats_are_split_by_operation();
    test_operation_table_overflow_goes_to_other();
    test_table_wide_macros_follow_build_mode();
    test_last_hold_ignores_inner_locks();
    printf("filesystem_lock tests passed (per_volume=%d narrow=%d)\n", (int)FILESYSTEM_LOCK_PER_VOLUME,
           (int)FILESYSTEM_LOCK_TABLE_NARROW);
    return 0;
}
