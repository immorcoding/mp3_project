/**
 * @file test_filesystem_lock.c
 * @brief Filesystem 私有锁的主机行为测试（spike #191）：统计、窗口、按任务的最近持锁、误调计数。
 *
 * @details
 *          同一份源码编译两次：默认全局一把锁，以及 `SPIKE_LOCK_PER_VOLUME=1` 的每卷一把加表锁。
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
 * @brief 锁个数与名字随编译模式变化；每卷一把时两把卷锁互相独立，表锁嵌套在卷锁内。
 */
static void test_lock_layout_matches_build_mode(void)
{
#if FILESYSTEM_LOCK_PER_VOLUME
    Service_Filesystem_SpikeLockStatsTypeDef sd_window;
    Service_Filesystem_SpikeLockStatsTypeDef flash_window;
    Service_Filesystem_SpikeLockStatsTypeDef table_window;

    assert(Service_Filesystem_SpikePerVolume());
    assert(Service_Filesystem_SpikeLockCount() == 3U);
    assert(strcmp(Service_Filesystem_SpikeLockName(0U), "SD") == 0);
    assert(strcmp(Service_Filesystem_SpikeLockName(1U), "FLASH") == 0);
    assert(strcmp(Service_Filesystem_SpikeLockName(2U), "TABLE") == 0);

    Service_Filesystem_SpikeWindowReset();
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "ReadFile");
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "WriteFile");
    filesystem_lock_table_acquire("OpenFile");
    filesystem_lock_table_release();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);

    assert(Service_Filesystem_SpikeGetLockStats(0U, &sd_window, NULL));
    assert(Service_Filesystem_SpikeGetLockStats(1U, &flash_window, NULL));
    assert(Service_Filesystem_SpikeGetLockStats(2U, &table_window, NULL));
    assert(sd_window.AcquireCount == 1U);
    assert(flash_window.AcquireCount == 1U);
    assert(table_window.AcquireCount == 1U);
    assert(strcmp(sd_window.HoldOperation, "ReadFile") == 0);
    assert(strcmp(flash_window.HoldOperation, "WriteFile") == 0);
    assert(strcmp(table_window.HoldOperation, "OpenFile") == 0);
#else
    assert(!Service_Filesystem_SpikePerVolume());
    assert(Service_Filesystem_SpikeLockCount() == 1U);
    assert(strcmp(Service_Filesystem_SpikeLockName(0U), "GLOBAL") == 0);
    assert(!Service_Filesystem_SpikeGetLockStats(1U, NULL, NULL));

    /* 全局一把：两个卷共用同一把锁，表锁是空操作，所以先后拿放不会重复获取。 */
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_FLASH, "WriteFile");
    filesystem_lock_table_acquire("OpenFile");
    filesystem_lock_table_release();
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_FLASH);
    filesystem_lock_acquire(SERVICE_FILESYSTEM_VOLUME_SD, "ReadFile");
    filesystem_lock_release(SERVICE_FILESYSTEM_VOLUME_SD);
#endif
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
    printf("filesystem_lock tests passed (per_volume=%d)\n", (int)FILESYSTEM_LOCK_PER_VOLUME);
    return 0;
}
