/**
 ******************************************************************************
 * @file    test_foreground_loop.c
 * @brief   前台任务一圈的判断（#218）：各应用的 step 轮流调用、取最小返回值当睡眠时长、通知提前叫醒、
 *          step 耗时的记录与告警（ARC-26），以及注册表的空字段与 `boot_init` 顺序。
 *
 * @details
 *          链接真实的 `foreground_loop.c`、`app_table_boot.c`、共用 FreeRTOS 桩，以及脚本化的周期计数器和
 *          Service Log 替身（`Tests/stubs/lock_env/`）。期望值写成字面量（来自 #202 G 节：“超过 50 ms 记 WARN，
 *          只在刷新最长值时记，不停机”），不用代码里的表达式重算。
 *          主机测试是单线程的：桩不切换任务，“别的任务在睡眠期间叫醒”靠阻塞钩子脚本化；真实的抢占、优先级、
 *          睡眠精度与 step 的真实耗时不在这里证明。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include "APP/apps/app_table.h"
#include "APP/tasks/foreground/foreground_loop.h"
#include "APP/tasks/foreground/foreground_task.h"
#include "Service/log/log_service.h"

/** @brief 一个假应用的前台 step：记下调用次数与顺序，脚本化返回值和耗时。 */
typedef struct
{
    uint32_t returns;     /**< 下一次调用返回的毫秒数。 */
    uint32_t cost_us;     /**< 一次调用让假周期计数器前进的微秒数。 */
    uint32_t calls;       /**< 被调用次数。 */
    uint32_t order_seen;  /**< 最近一次被调用时的全局序号（从 1 起）。 */
    bool wake_inside;     /**< 被调用时顺便调一次 foreground_task_wake()。 */
} FakeApp;

static FakeApp apps_state[4];
static uint32_t global_order;
static uint32_t boot_log[8];
static uint32_t boot_log_count;

/**
 * @brief 假 step 的共同实现。
 * @param[in,out] app 应用记录。
 * @return 脚本里的等待毫秒数。
 */
static uint32_t fake_step(FakeApp *app)
{
    app->calls++;
    app->order_seen = ++global_order;
    lock_env_advance_us(app->cost_us);
    if (app->wake_inside)
    {
        foreground_task_wake();
    }
    return app->returns;
}

/** @brief 第 0 个假应用的 step。 @return 脚本里的等待毫秒数。 */
static uint32_t app0_step(void)
{
    return fake_step(&apps_state[0]);
}

/** @brief 第 1 个假应用的 step。 @return 脚本里的等待毫秒数。 */
static uint32_t app1_step(void)
{
    return fake_step(&apps_state[1]);
}

/** @brief 第 2 个假应用的 step。 @return 脚本里的等待毫秒数。 */
static uint32_t app2_step(void)
{
    return fake_step(&apps_state[2]);
}

/** @brief 第 0 个假应用的 boot_init：记下被调用的顺序。 */
static void boot0(void)
{
    boot_log[boot_log_count++] = 0U;
}

/** @brief 第 1 个假应用的 boot_init：记下被调用的顺序。 */
static void boot1(void)
{
    boot_log[boot_log_count++] = 1U;
}

/** @brief 第 3 个假应用的 boot_init：记下被调用的顺序。 */
static void boot3(void)
{
    boot_log[boot_log_count++] = 3U;
}

/** @brief 阻塞钩子的记录：看到的等待、以及是否替“别的任务”在睡眠期间叫醒。 */
typedef struct
{
    uint32_t waits;
    TickType_t last_ticks;
    UBaseType_t last_index;
    bool wake_during_wait;
} WaitRecord;

static WaitRecord waits_seen;
static uint32_t after_round_calls;
static uint32_t after_round_waits_seen; /* after_round 被调用时，已经发生的等待次数。 */

/**
 * @brief 阻塞钩子：记录等待，必要时替别的任务叫醒前台任务。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context WaitRecord。
 */
static void block_hook(const FreertosStub_Wait *wait, void *context)
{
    WaitRecord *record = (WaitRecord *)context;

    assert(wait->kind == FREERTOS_STUB_WAIT_NOTIFICATION);
    record->waits++;
    record->last_ticks = wait->ticks;
    record->last_index = wait->index;
    if (record->wake_during_wait)
    {
        foreground_task_wake();
    }
}

/** @brief 每圈调一次的回调：记下调用次数，以及调用时已发生的等待次数（证明它先于等待）。 */
static void after_round(void)
{
    after_round_calls++;
    after_round_waits_seen = waits_seen.waits;
}

/**
 * @brief 开始一个场景：应用记录清零（默认等通知、不耗时），桩、周期计数器与日志复位，当前任务绑定为前台任务。
 */
static void begin_scenario(void)
{
    for (size_t index = 0U; index < 4U; index++)
    {
        apps_state[index] = (FakeApp){FOREGROUND_WAIT_FOREVER, 0U, 0U, 0U, false};
    }
    global_order = 0U;
    boot_log_count = 0U;
    after_round_calls = 0U;
    after_round_waits_seen = 0U;
    waits_seen = (WaitRecord){0U, 0U, 0U, false};
    freertos_stub_reset();
    freertos_stub_set_tick(1000U);
    freertos_stub_set_block_hook(block_hook, &waits_seen);
    lock_env_counter_reset();
    lock_env_log_reset();
    foreground_loop_reset();
    foreground_loop_bind(xTaskGetCurrentTaskHandle());
}

/** @brief 全部轮流：每圈每个应用恰好调一次，按表里的顺序，第一个返回 0 也不短路，空字段跳过。 */
static void test_every_app_is_called_once_per_round_in_order_and_null_fields_are_skipped(void)
{
    const AppTable_EntryTypeDef table[] = {
        {"a", NULL, NULL, NULL, app0_step, NULL},
        {"empty", NULL, NULL, NULL, NULL, NULL},
        {"b", NULL, NULL, NULL, app1_step, NULL},
        {"c", NULL, NULL, NULL, app2_step, NULL},
    };

    begin_scenario();
    apps_state[0].returns = 0U; /* 马上再叫我，但这一圈后面的应用照样要调。 */
    apps_state[1].returns = 40U;
    apps_state[2].returns = FOREGROUND_WAIT_FOREVER;

    (void)foreground_loop_run_steps(table, 4U);

    assert(apps_state[0].calls == 1U);
    assert(apps_state[1].calls == 1U);
    assert(apps_state[2].calls == 1U);
    assert(apps_state[0].order_seen == 1U);
    assert(apps_state[1].order_seen == 2U);
    assert(apps_state[2].order_seen == 3U);
}

/** @brief 睡眠时长取最小返回值；0 最小；全是“等通知”或没有应用时是“等通知”。 */
static void test_the_wait_is_the_smallest_return_value(void)
{
    const AppTable_EntryTypeDef table[] = {
        {"a", NULL, NULL, NULL, app0_step, NULL},
        {"b", NULL, NULL, NULL, app1_step, NULL},
        {"c", NULL, NULL, NULL, app2_step, NULL},
    };

    begin_scenario();
    apps_state[0].returns = 250U;
    apps_state[1].returns = 40U;
    apps_state[2].returns = FOREGROUND_WAIT_FOREVER;
    assert(foreground_loop_run_steps(table, 3U) == 40U);

    apps_state[1].returns = 0U;
    assert(foreground_loop_run_steps(table, 3U) == 0U);

    apps_state[0].returns = FOREGROUND_WAIT_FOREVER;
    apps_state[1].returns = FOREGROUND_WAIT_FOREVER;
    assert(foreground_loop_run_steps(table, 3U) == FOREGROUND_WAIT_FOREVER);

    assert(foreground_loop_run_steps(table, 0U) == FOREGROUND_WAIT_FOREVER);
    assert(foreground_loop_run_steps(NULL, 0U) == FOREGROUND_WAIT_FOREVER);
}

/** @brief 毫秒换等待 tick：0 为 0，等通知为 portMAX_DELAY，非零毫秒向上取整、不会变成 0。 */
static void test_wait_ticks_conversion(void)
{
    assert(foreground_loop_wait_ticks(0U) == 0U);
    assert(foreground_loop_wait_ticks(1U) == 1U);
    assert(foreground_loop_wait_ticks(10U) == 10U);
    assert(foreground_loop_wait_ticks(FOREGROUND_WAIT_FOREVER) == portMAX_DELAY);
    assert(foreground_loop_wait_ticks(FOREGROUND_WAIT_FOREVER - 1U) == (TickType_t)(portMAX_DELAY - 1U));
}

/** @brief 一圈 = 调一遍 step、调一次回调、再按最小返回值等通知（回调先于等待）；等的是前台任务的 0 号槽。 */
static void test_one_round_runs_steps_then_the_callback_then_waits_for_notification(void)
{
    const AppTable_EntryTypeDef table[] = {{"a", NULL, NULL, NULL, app0_step, NULL}};

    begin_scenario();
    apps_state[0].returns = 25U;

    assert(!foreground_loop_step(table, 1U, after_round));

    assert(apps_state[0].calls == 1U);
    assert(after_round_calls == 1U);
    assert(after_round_waits_seen == 0U);
    assert(waits_seen.waits == 1U);
    assert(waits_seen.last_ticks == 25U);
    assert(waits_seen.last_index == (UBaseType_t)FOREGROUND_NOTIFY_EVENT);

    /* 回调可以是 NULL。 */
    assert(!foreground_loop_step(table, 1U, NULL));
    assert(after_round_calls == 1U);
}

/** @brief 睡眠期间有人叫醒：等待提前返回（真）；叫醒在 step 运行期间到达也不丢，下一次等待立刻返回。 */
static void test_a_wake_during_the_wait_or_during_a_step_is_not_lost(void)
{
    const AppTable_EntryTypeDef table[] = {{"a", NULL, NULL, NULL, app0_step, NULL}};

    begin_scenario();
    apps_state[0].returns = FOREGROUND_WAIT_FOREVER;
    waits_seen.wake_during_wait = true;
    assert(foreground_loop_step(table, 1U, NULL));

    begin_scenario();
    apps_state[0].returns = FOREGROUND_WAIT_FOREVER;
    apps_state[0].wake_inside = true; /* step 自己改了状态，调 wake：通知留在槽里，等待立刻返回。 */
    assert(foreground_loop_step(table, 1U, NULL));
    assert(waits_seen.waits == 0U); /* 通知已在槽里，睡眠根本没有开始，也没有等满。 */
    assert(xTaskGetTickCount() == 1000U);

    /* 通知被取走了：下一圈（step 不再叫醒）进入等待。 */
    apps_state[0].wake_inside = false;
    assert(!foreground_loop_step(table, 1U, NULL));
    assert(waits_seen.waits == 1U);
    assert(waits_seen.last_ticks == portMAX_DELAY);
}

/** @brief 任务入口还没绑定句柄时 wake 是空操作（任务第一圈本来就调所有应用）；绑定之后才有通知。 */
static void test_wake_before_bind_is_a_noop(void)
{
    TaskHandle_t task;

    begin_scenario();
    task = xTaskGetCurrentTaskHandle();
    foreground_loop_bind(NULL);
    foreground_task_wake();
    assert(!freertos_stub_notification_pending(task, (UBaseType_t)FOREGROUND_NOTIFY_EVENT));

    foreground_loop_bind(task);
    foreground_task_wake();
    assert(freertos_stub_notification_pending(task, (UBaseType_t)FOREGROUND_NOTIFY_EVENT));
}

/** @brief 每个应用记下自己 step 的最长耗时（微秒）；最长值只升不降。 */
static void test_each_app_keeps_its_own_longest_step_time(void)
{
    const AppTable_EntryTypeDef table[] = {
        {"a", NULL, NULL, NULL, app0_step, NULL},
        {"b", NULL, NULL, NULL, app1_step, NULL},
    };

    begin_scenario();
    apps_state[0].cost_us = 1200U;
    apps_state[1].cost_us = 300U;
    (void)foreground_loop_run_steps(table, 2U);
    assert(foreground_loop_step_max_us(0U) == 1200U);
    assert(foreground_loop_step_max_us(1U) == 300U);

    apps_state[0].cost_us = 800U; /* 更短：最长值不降。 */
    apps_state[1].cost_us = 450U;
    (void)foreground_loop_run_steps(table, 2U);
    assert(foreground_loop_step_max_us(0U) == 1200U);
    assert(foreground_loop_step_max_us(1U) == 450U);

    assert(foreground_loop_step_max_us(7U) == 0U);  /* 没有这个应用。 */
    assert(foreground_loop_step_max_us(99U) == 0U); /* 越界。 */
}

/** @brief 超过 50 ms 且刷新了最长值才记 WARN；没刷新（更短或相等）不记；再刷新再记；整个过程不停机（返回值照常）。 */
static void test_slow_step_warns_only_when_the_longest_value_is_refreshed(void)
{
    const AppTable_EntryTypeDef table[] = {{"music", NULL, NULL, NULL, app0_step, NULL}};

    begin_scenario();
    apps_state[0].returns = 7U;

    apps_state[0].cost_us = 50000U; /* 恰好等于阈值：不超过，不记 WARN。 */
    assert(foreground_loop_run_steps(table, 1U) == 7U);
    assert(lock_env_log_count() == 0U);

    apps_state[0].cost_us = 60000U; /* 超过且刷新最长值：WARN。 */
    assert(foreground_loop_run_steps(table, 1U) == 7U);
    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_WARN);
    assert(strcmp(lock_env_log_tag(0U), "FOREGROUND") == 0);
    assert(strcmp(lock_env_log_text(0U), "Foreground step slow app=music us=60000 limit=50000") == 0);

    apps_state[0].cost_us = 60000U; /* 相等：不是刷新，不记。 */
    (void)foreground_loop_run_steps(table, 1U);
    apps_state[0].cost_us = 55000U; /* 更短：不记。 */
    (void)foreground_loop_run_steps(table, 1U);
    assert(lock_env_log_count() == 1U);

    apps_state[0].cost_us = 70000U; /* 再刷新：再记一行。 */
    assert(foreground_loop_run_steps(table, 1U) == 7U);
    assert(lock_env_log_count() == 2U);
    assert(strcmp(lock_env_log_text(1U), "Foreground step slow app=music us=70000 limit=50000") == 0);
}

/** @brief 告警行不超过 127 个字符（Service Log 单条容量 128 B），应用名过长时被截断而不是溢出。 */
static void test_warn_line_fits_the_log_capacity_even_with_a_long_app_name(void)
{
    const AppTable_EntryTypeDef table[] = {
        {"an-application-name-that-is-far-too-long-to-be-a-sensible-registry-entry-and-keeps-going-on-and-on-and-on-and-on",
         NULL, NULL, NULL, app0_step, NULL},
    };

    begin_scenario();
    apps_state[0].cost_us = 4000000000U;
    (void)foreground_loop_run_steps(table, 1U);
    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_longest_text() <= 127U);
}

/** @brief 最长值刷新但没超阈值：不立刻记，汇总成一行 INFO，且至多每 10 s 一行，没有新刷新就不记。 */
static void test_longest_values_below_the_threshold_are_summarised_in_one_line_per_period(void)
{
    const AppTable_EntryTypeDef table[] = {
        {"music", NULL, NULL, NULL, app0_step, NULL},
        {"books", NULL, NULL, NULL, app1_step, NULL},
    };

    begin_scenario();
    apps_state[0].cost_us = 1200U;
    apps_state[1].cost_us = 300U;
    (void)foreground_loop_run_steps(table, 2U);
    assert(lock_env_log_count() == 0U); /* 没超阈值，step 里不记。 */

    foreground_loop_report(5000U);      /* 周期没到（10 s）。 */
    assert(lock_env_log_count() == 0U);
    foreground_loop_report(10000U);
    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_INFO);
    assert(strcmp(lock_env_log_tag(0U), "FOREGROUND") == 0);
    assert(strcmp(lock_env_log_text(0U), "Foreground step max music=1200us books=300us") == 0);

    foreground_loop_report(30000U); /* 没有新刷新：不记，多久都不记。 */
    assert(lock_env_log_count() == 1U);

    apps_state[1].cost_us = 900U; /* 刷新：这一行的节拍从上一行（30000 ms 时没有记）仍是 10000 ms 起算，周期早已满。 */
    (void)foreground_loop_run_steps(table, 2U);
    foreground_loop_report(30000U);
    assert(lock_env_log_count() == 2U); /* 记一行，两个应用的当前最长值都在里面。 */
    assert(strcmp(lock_env_log_text(1U), "Foreground step max music=1200us books=900us") == 0);

    apps_state[0].cost_us = 1500U; /* 再刷新，但距上一行才 3 s：先不记，满周期再记。 */
    (void)foreground_loop_run_steps(table, 2U);
    foreground_loop_report(33000U);
    assert(lock_env_log_count() == 2U);
    foreground_loop_report(40000U);
    assert(lock_env_log_count() == 3U);
    assert(strcmp(lock_env_log_text(2U), "Foreground step max music=1500us books=900us") == 0);
}

/** @brief 汇总行放不下时在末尾打 `~`，不超过 127 个字符。 */
static void test_summary_line_is_truncated_visibly_not_overflowed(void)
{
    static const char *const names[] = {"application-one", "application-two", "application-three", "application-four",
                                        "application-five", "application-six", "application-seven", "application-eight"};
    AppTable_EntryTypeDef table[8];

    begin_scenario();
    for (size_t index = 0U; index < 8U; index++)
    {
        table[index] = (AppTable_EntryTypeDef){names[index], NULL, NULL, NULL, app0_step, NULL};
    }
    apps_state[0].cost_us = 4321U; /* 8 个应用各记 4321 us，都没超阈值。 */
    (void)foreground_loop_run_steps(table, 8U);
    foreground_loop_report(10000U);
    assert(lock_env_log_count() >= 1U);
    assert(lock_env_log_longest_text() <= 127U);
    assert(strchr(lock_env_log_text(lock_env_log_count() - 1U), '~') != NULL);
}

/** @brief 周期计数器没启动（频率 0）：耗时记 0，不会误报，也不会除零。 */
static void test_unavailable_cycle_counter_records_zero_without_warning(void)
{
    const AppTable_EntryTypeDef table[] = {{"music", NULL, NULL, NULL, app0_step, NULL}};

    begin_scenario();
    lock_env_set_counter_unavailable(true);
    apps_state[0].cost_us = 90000U;
    (void)foreground_loop_run_steps(table, 1U);
    assert(foreground_loop_step_max_us(0U) == 0U);
    assert(lock_env_log_count() == 0U);
}

/** @brief 周期计数器 32 位回绕：耗时用无符号差，仍然正确。 */
static void test_cycle_counter_wrap_does_not_inflate_a_step_time(void)
{
    const AppTable_EntryTypeDef table[] = {{"music", NULL, NULL, NULL, app0_step, NULL}};

    begin_scenario();
    lock_env_advance_us(8947000U); /* 480 MHz 下约 4.29e9 周期：让计数器贴近 32 位上限。 */
    apps_state[0].cost_us = 3000U;
    (void)foreground_loop_run_steps(table, 1U);
    assert(foreground_loop_step_max_us(0U) == 3000U);
}

/** @brief 注册表的 boot_init 按表的顺序逐行调用，空字段跳过，每行只调一次。 */
static void test_boot_init_is_called_in_table_order_and_null_fields_are_skipped(void)
{
    const AppTable_EntryTypeDef table[] = {
        {"a", boot0, NULL, NULL, NULL, NULL},
        {"none", NULL, NULL, NULL, app0_step, NULL},
        {"b", boot1, NULL, NULL, NULL, NULL},
        {"c", boot3, NULL, NULL, NULL, NULL},
    };

    begin_scenario();
    app_table_boot_init_rows(table, 4U);

    assert(boot_log_count == 3U);
    assert(boot_log[0] == 0U);
    assert(boot_log[1] == 1U);
    assert(boot_log[2] == 3U);
    assert(apps_state[0].calls == 0U); /* boot_init 不会顺手调 step。 */

    boot_log_count = 0U;
    app_table_boot_init_rows(NULL, 0U);
    assert(boot_log_count == 0U);
}

/**
 * @brief 主机测试入口。
 * @return 0 表示全部通过。
 */
int main(void)
{
    test_every_app_is_called_once_per_round_in_order_and_null_fields_are_skipped();
    test_the_wait_is_the_smallest_return_value();
    test_wait_ticks_conversion();
    test_one_round_runs_steps_then_the_callback_then_waits_for_notification();
    test_a_wake_during_the_wait_or_during_a_step_is_not_lost();
    test_wake_before_bind_is_a_noop();
    test_each_app_keeps_its_own_longest_step_time();
    test_slow_step_warns_only_when_the_longest_value_is_refreshed();
    test_warn_line_fits_the_log_capacity_even_with_a_long_app_name();
    test_longest_values_below_the_threshold_are_summarised_in_one_line_per_period();
    test_summary_line_is_truncated_visibly_not_overflowed();
    test_unavailable_cycle_counter_records_zero_without_warning();
    test_cycle_counter_wrap_does_not_inflate_a_step_time();
    test_boot_init_is_called_in_table_order_and_null_fields_are_skipped();
    puts("foreground_loop: OK");
    return 0;
}
