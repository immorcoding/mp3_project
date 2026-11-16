/**
 ******************************************************************************
 * @file    test_volume_reconcile.c
 * @brief   卷对账与消抖计时纯函数的主机测试：#195 的 5 行表、刷新失败、路径名、静默时间的回绕。
 *
 * @details
 *          只经 `volume_reconcile.h` 的公开函数观察。期望值来自 #195 / #202 C 节的表，不用代码里的
 *          表达式重算。纯函数没有任务与锁，所以这里的通过就是完整的证明（不依赖单线程假设）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/volume/volume_reconcile.h"

/**
 * @brief 组一份对账输入：刷新成功。
 * @param[in] mounted 刷新之前卷是否挂着。
 * @param[in] present 卡检测脚是否表示有卡。
 * @param[in] ready 设备是否就绪。
 * @param[in] reinitialized 这次刷新是否重新初始化过。
 * @return 输入。
 */
static Volume_ReconcileInputTypeDef input(bool mounted, bool present, bool ready, bool reinitialized)
{
    Volume_ReconcileInputTypeDef value = {mounted, true, present, ready, reinitialized};

    return value;
}

/**
 * @brief 表第 1 行：卷挂着，卡不在，或设备没就绪 → 卸载。
 */
static void test_mounted_card_absent_or_not_ready_unmounts(void)
{
    Volume_ReconcileInputTypeDef absent = input(true, false, false, false);
    Volume_ReconcileInputTypeDef not_ready = input(true, true, false, false);
    Volume_ReconcileInputTypeDef absent_but_flags_ready = input(true, false, true, true);

    assert(volume_reconcile(&absent) == VOLUME_PATH_UNMOUNT);
    assert(volume_reconcile(&not_ready) == VOLUME_PATH_UNMOUNT);
    /* 卡不在时，就绪、重新初始化这些位没有意义，以“不在”为准。 */
    assert(volume_reconcile(&absent_but_flags_ready) == VOLUME_PATH_UNMOUNT);
}

/**
 * @brief 表第 2 行：卷挂着，卡在且就绪，这次刷新重新初始化过（换卡或被吞的拔出）→ 卸载再挂载。
 */
static void test_mounted_card_reinitialized_remounts(void)
{
    Volume_ReconcileInputTypeDef swapped = input(true, true, true, true);

    assert(volume_reconcile(&swapped) == VOLUME_PATH_REMOUNT);
}

/**
 * @brief 表第 3 行：卷挂着，卡在，没变化（假抖动）→ 只开闸，不卸载也不挂载。
 */
static void test_mounted_card_unchanged_only_opens_gate(void)
{
    Volume_ReconcileInputTypeDef bounce = input(true, true, true, false);

    assert(volume_reconcile(&bounce) == VOLUME_PATH_GATE_ONLY);
    assert(!volume_path_unmounts(VOLUME_PATH_GATE_ONLY));
    assert(!volume_path_mounts(VOLUME_PATH_GATE_ONLY));
}

/**
 * @brief 表第 4 行：卷没挂，卡在且就绪 → 挂载（重新初始化与否都一样）。
 */
static void test_unmounted_card_present_and_ready_mounts(void)
{
    Volume_ReconcileInputTypeDef inserted = input(false, true, true, true);
    Volume_ReconcileInputTypeDef inserted_quietly = input(false, true, true, false);

    assert(volume_reconcile(&inserted) == VOLUME_PATH_MOUNT);
    assert(volume_reconcile(&inserted_quietly) == VOLUME_PATH_MOUNT);
}

/**
 * @brief 表第 5 行：卷没挂，卡不在 → 无动作。卡在但设备没就绪（一直 ERROR、没有边沿）同样不动作，不自动恢复。
 */
static void test_unmounted_without_usable_card_does_nothing(void)
{
    Volume_ReconcileInputTypeDef absent = input(false, false, false, false);
    Volume_ReconcileInputTypeDef stuck_error = input(false, true, false, false);

    assert(volume_reconcile(&absent) == VOLUME_PATH_IDLE);
    assert(volume_reconcile(&stuck_error) == VOLUME_PATH_IDLE);
}

/**
 * @brief 刷新失败但卡不在：卷挂着时同样卸载（被吞的拔出之后刷新又失败的情形）。
 *        刷新失败时结构里的就绪位不可信：卷挂着一律卸载，没挂一律无动作（不挂载）。
 */
static void test_refresh_failure_unmounts_a_mounted_volume_and_never_mounts(void)
{
    Volume_ReconcileInputTypeDef failed_absent = {true, false, false, false, false};
    Volume_ReconcileInputTypeDef failed_present = {true, false, true, false, false};
    Volume_ReconcileInputTypeDef failed_claims_ready = {true, false, true, true, true};
    Volume_ReconcileInputTypeDef failed_while_unmounted = {false, false, true, true, true};

    assert(volume_reconcile(&failed_absent) == VOLUME_PATH_UNMOUNT);
    assert(volume_reconcile(&failed_present) == VOLUME_PATH_UNMOUNT);
    assert(volume_reconcile(&failed_claims_ready) == VOLUME_PATH_UNMOUNT);
    assert(volume_reconcile(&failed_while_unmounted) == VOLUME_PATH_IDLE);
}

/**
 * @brief 每条路径要做的动作：卸载、挂载各自对应哪几条，日志里的路径名是 R3.1–R3.5 判读的依据，不能改动。
 */
static void test_path_actions_and_names(void)
{
    assert(volume_path_unmounts(VOLUME_PATH_UNMOUNT));
    assert(volume_path_unmounts(VOLUME_PATH_REMOUNT));
    assert(!volume_path_unmounts(VOLUME_PATH_MOUNT));
    assert(!volume_path_unmounts(VOLUME_PATH_IDLE));

    assert(volume_path_mounts(VOLUME_PATH_MOUNT));
    assert(volume_path_mounts(VOLUME_PATH_REMOUNT));
    assert(!volume_path_mounts(VOLUME_PATH_UNMOUNT));
    assert(!volume_path_mounts(VOLUME_PATH_IDLE));

    assert(strcmp(volume_path_name(VOLUME_PATH_IDLE), "idle") == 0);
    assert(strcmp(volume_path_name(VOLUME_PATH_GATE_ONLY), "gate_only") == 0);
    assert(strcmp(volume_path_name(VOLUME_PATH_MOUNT), "mount") == 0);
    assert(strcmp(volume_path_name(VOLUME_PATH_UNMOUNT), "unmount") == 0);
    assert(strcmp(volume_path_name(VOLUME_PATH_REMOUNT), "remount") == 0);
}

/**
 * @brief 静默时间：从最后一个边沿起算，剩余随 tick 递减，到点为 0；新边沿重新计时。
 */
static void test_debounce_remaining_counts_down_from_the_last_edge(void)
{
    assert(volume_debounce_remaining(100U, 100U, 30U) == 30U);
    assert(volume_debounce_remaining(110U, 100U, 30U) == 20U);
    assert(volume_debounce_remaining(129U, 100U, 30U) == 1U);
    assert(volume_debounce_remaining(130U, 100U, 30U) == 0U);
    assert(volume_debounce_remaining(500U, 100U, 30U) == 0U);

    /* 新边沿（起点后移）让剩余重新回到整段静默时间。 */
    assert(volume_debounce_remaining(125U, 120U, 30U) == 25U);
}

/**
 * @brief tick 计数回绕：起点在回绕之前、现在在回绕之后，经过的 tick 仍按无符号差值算。
 */
static void test_debounce_remaining_survives_tick_wraparound(void)
{
    const TickType_t last_edge = (TickType_t)0xFFFFFFF0U;

    assert(volume_debounce_remaining((TickType_t)0xFFFFFFF8U, last_edge, 30U) == 22U);
    assert(volume_debounce_remaining((TickType_t)0U, last_edge, 30U) == 14U);
    assert(volume_debounce_remaining((TickType_t)13U, last_edge, 30U) == 1U);
    assert(volume_debounce_remaining((TickType_t)14U, last_edge, 30U) == 0U);
    assert(volume_debounce_remaining((TickType_t)100U, last_edge, 30U) == 0U);
}

/**
 * @brief 依次运行用例。
 * @return 全部通过时为 0，断言失败时进程中止。
 */
int main(void)
{
    test_mounted_card_absent_or_not_ready_unmounts();
    test_mounted_card_reinitialized_remounts();
    test_mounted_card_unchanged_only_opens_gate();
    test_unmounted_card_present_and_ready_mounts();
    test_unmounted_without_usable_card_does_nothing();
    test_refresh_failure_unmounts_a_mounted_volume_and_never_mounts();
    test_path_actions_and_names();
    test_debounce_remaining_counts_down_from_the_last_edge();
    test_debounce_remaining_survives_tick_wraparound();
    puts("volume_reconcile: all passed");
    return 0;
}
