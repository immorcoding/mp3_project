/**
 ******************************************************************************
 * @file    test_background_loop.c
 * @brief   后台任务一圈的判断（#213）：每圈所有作业各调一次、取最小返回值当睡眠时长、任何通知都提前叫醒。
 *
 * @details
 *          链接真实的 `background_loop.c` 与共用 FreeRTOS 桩。期望值写成字面量（来自 #202 F 节：
 *          "后台任务每圈把所有作业各调一次，取最小返回值作为睡眠时长，任何通知都提前叫醒"），不用代码里的表达式重算。
 *          主机测试是单线程的：桩不切换任务，"别的任务在睡眠期间调了 wake"靠阻塞钩子脚本化；
 *          真实的抢占、优先级与睡眠精度不在这里证明。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "freertos_stub.h"

#include "APP/tasks/background/background_loop.h"
#include "APP/tasks/background/background_task.h"
#include "APP/tasks/background/background_task_config.h"
#include "APP/tasks/volume/volume_event.h"

/** @brief 一个假作业：记下被调用的次数与顺序，返回脚本里的等待时长。 */
typedef struct
{
    uint32_t returns;       /**< 下一次调用返回的毫秒数。 */
    uint32_t calls;         /**< 被调用次数。 */
    uint32_t order_seen;    /**< 最近一次被调用时的全局调用序号（从 1 起）。 */
    bool wake_inside;       /**< 被调用时顺便调一次 background_task_wake()（模拟作业自己改了状态）。 */
} FakeJob;

static FakeJob jobs_state[4];
static uint32_t global_order;

/**
 * @brief 假作业的共同实现。
 * @param[in,out] job 作业记录。
 * @return 脚本里的等待毫秒数。
 */
static uint32_t fake_step(FakeJob *job)
{
    job->calls++;
    job->order_seen = ++global_order;
    if (job->wake_inside)
    {
        background_task_wake();
    }
    return job->returns;
}

/** @brief 表里的第 0 个假作业。 @return 脚本里的等待毫秒数。 */
static uint32_t job0(void)
{
    return fake_step(&jobs_state[0]);
}

/** @brief 表里的第 1 个假作业。 @return 脚本里的等待毫秒数。 */
static uint32_t job1(void)
{
    return fake_step(&jobs_state[1]);
}

/** @brief 表里的第 2 个假作业。 @return 脚本里的等待毫秒数。 */
static uint32_t job2(void)
{
    return fake_step(&jobs_state[2]);
}

/** @brief 表里的第 3 个假作业。 @return 脚本里的等待毫秒数。 */
static uint32_t job3(void)
{
    return fake_step(&jobs_state[3]);
}

/** @brief 阻塞钩子的记录：看到的等待、以及是否替"别的任务"在睡眠期间叫醒。 */
typedef struct
{
    uint32_t waits;
    TickType_t last_ticks;
    UBaseType_t last_index;
    bool wake_during_wait;
} WaitRecord;

static WaitRecord waits_seen;

/**
 * @brief 阻塞钩子：记录等待，必要时替别的任务叫醒后台任务。
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
        background_task_wake();
    }
}

/**
 * @brief 开始一个场景：作业记录清零（默认等通知），桩复位，把当前任务绑定为后台任务。
 */
static void begin_scenario(void)
{
    for (size_t index = 0U; index < 4U; index++)
    {
        jobs_state[index] = (FakeJob){BACKGROUND_WAIT_FOREVER, 0U, 0U, false};
    }
    global_order = 0U;
    waits_seen = (WaitRecord){0U, 0U, 0U, false};
    freertos_stub_reset();
    freertos_stub_set_tick(1000U);
    freertos_stub_set_block_hook(block_hook, &waits_seen);
    background_loop_bind(xTaskGetCurrentTaskHandle());
}

/** @brief 全部轮流：每圈每个作业恰好调一次，按表里的顺序，第一个返回 0 也不短路。 */
static void test_every_job_is_called_once_per_lap_in_order(void)
{
    const background_job_step_t table[] = {job0, job1, job2};

    begin_scenario();
    jobs_state[0].returns = 0U; /* 马上再叫我，但这一圈后面的作业照样要调。 */
    jobs_state[1].returns = 40U;
    jobs_state[2].returns = BACKGROUND_WAIT_FOREVER;

    (void)background_loop_run_jobs(table, 3U);

    assert(jobs_state[0].calls == 1U);
    assert(jobs_state[1].calls == 1U);
    assert(jobs_state[2].calls == 1U);
    assert(jobs_state[0].order_seen == 1U);
    assert(jobs_state[1].order_seen == 2U);
    assert(jobs_state[2].order_seen == 3U);

    (void)background_loop_run_jobs(table, 3U);
    assert(jobs_state[0].calls == 2U);
    assert(jobs_state[1].calls == 2U);
    assert(jobs_state[2].calls == 2U);
}

/** @brief 最小等待：取各作业返回值里最小的，0 最小，等通知的哨兵最大；表空或全是空项是等通知。 */
static void test_lap_returns_the_smallest_wait(void)
{
    const background_job_step_t table[] = {job0, job1, job2, job3};
    const background_job_step_t with_gaps[] = {NULL, job1, NULL};

    begin_scenario();
    jobs_state[0].returns = 500U;
    jobs_state[1].returns = 20U;
    jobs_state[2].returns = BACKGROUND_WAIT_FOREVER;
    jobs_state[3].returns = 300U;
    assert(background_loop_run_jobs(table, 4U) == 20U);

    jobs_state[0].returns = BACKGROUND_WAIT_FOREVER;
    jobs_state[1].returns = BACKGROUND_WAIT_FOREVER;
    jobs_state[2].returns = BACKGROUND_WAIT_FOREVER;
    jobs_state[3].returns = BACKGROUND_WAIT_FOREVER;
    assert(background_loop_run_jobs(table, 4U) == BACKGROUND_WAIT_FOREVER);

    jobs_state[2].returns = 0U;
    assert(background_loop_run_jobs(table, 4U) == 0U);

    assert(background_loop_run_jobs(table, 0U) == BACKGROUND_WAIT_FOREVER);
    assert(background_loop_run_jobs(NULL, 0U) == BACKGROUND_WAIT_FOREVER);

    jobs_state[1].returns = 77U;
    assert(background_loop_run_jobs(with_gaps, 3U) == 77U);
    assert(background_loop_run_jobs(&with_gaps[0], 1U) == BACKGROUND_WAIT_FOREVER);
}

/** @brief 毫秒到等待 tick：0 不等，哨兵无限等，非零不会被取整成 0（1 kHz 节拍：1 ms = 1 tick）。 */
static void test_wait_ticks_conversion(void)
{
    assert(background_loop_wait_ticks(0U) == 0U);
    assert(background_loop_wait_ticks(1U) == 1U);
    assert(background_loop_wait_ticks(10U) == 10U);
    assert(background_loop_wait_ticks(5000U) == 5000U);
    assert(background_loop_wait_ticks(BACKGROUND_WAIT_FOREVER) == portMAX_DELAY);
    /* 比哨兵小一点的有限等待不能被当成无限等待。 */
    assert(background_loop_wait_ticks(BACKGROUND_WAIT_FOREVER - 1U) == (portMAX_DELAY - 1U));
}

/** @brief 没人叫醒：睡满最小等待（桩里 tick 前进 20），返回假；作业这一圈调了一次。 */
static void test_step_sleeps_the_smallest_wait_when_nobody_wakes(void)
{
    const background_job_step_t table[] = {job0, job1};

    begin_scenario();
    jobs_state[0].returns = 300U;
    jobs_state[1].returns = 20U;

    assert(!background_loop_step(table, 2U));
    assert(waits_seen.waits == 1U);
    assert(waits_seen.last_ticks == 20U);
    assert(waits_seen.last_index == (UBaseType_t)BACKGROUND_NOTIFY_EVENT);
    assert(xTaskGetTickCount() == 1020U);
    assert(jobs_state[0].calls == 1U);
    assert(jobs_state[1].calls == 1U);
}

/** @brief 全部等通知：无限等待，没有定时叫醒；没有作业（默认构建）同样一直阻塞。 */
static void test_step_blocks_forever_when_every_job_waits_for_a_notification(void)
{
    const background_job_step_t table[] = {job0, job1};

    begin_scenario();
    assert(!background_loop_step(table, 2U));
    assert(waits_seen.waits == 1U);
    assert(waits_seen.last_ticks == portMAX_DELAY);
    assert(freertos_stub_unsatisfied_infinite_waits() == 1U);

    assert(!background_loop_step(NULL, 0U));
    assert(waits_seen.waits == 2U);
    assert(waits_seen.last_ticks == portMAX_DELAY);
}

/** @brief 通知提前醒：睡眠期间有人叫醒，返回真，时间没有前进到睡满；无限等待也叫得醒。 */
static void test_step_wakes_early_on_notification(void)
{
    const background_job_step_t table[] = {job0, job1};

    begin_scenario();
    jobs_state[0].returns = 5000U;
    jobs_state[1].returns = 20U;
    waits_seen.wake_during_wait = true;

    assert(background_loop_step(table, 2U));
    assert(waits_seen.last_ticks == 20U);
    assert(xTaskGetTickCount() == 1000U);

    jobs_state[0].returns = BACKGROUND_WAIT_FOREVER;
    jobs_state[1].returns = BACKGROUND_WAIT_FOREVER;
    assert(background_loop_step(table, 2U));
    assert(waits_seen.last_ticks == portMAX_DELAY);
    assert(freertos_stub_unsatisfied_infinite_waits() == 0U);
}

/** @brief 通知在作业运行期间到达（不在睡眠里）不会丢：这一圈睡眠立刻结束，下一圈马上再调所有作业。 */
static void test_notification_during_the_lap_is_not_lost(void)
{
    const background_job_step_t table[] = {job0, job1};

    begin_scenario();
    jobs_state[0].returns = 5000U;
    jobs_state[1].returns = 5000U;
    jobs_state[1].wake_inside = true; /* 作业自己改了状态，要求再调一遍。 */

    assert(background_loop_step(table, 2U));
    assert(xTaskGetTickCount() == 1000U);
    assert(waits_seen.waits == 0U); /* 通知已在，睡眠根本没有开始。 */

    /* 通知被取走了：下一圈（作业不再叫醒）睡满。 */
    jobs_state[1].wake_inside = false;
    assert(!background_loop_step(table, 2U));
    assert(xTaskGetTickCount() == 6000U);
}

/** @brief 连叫几次只算一次醒：置位合并，取走后下一圈不再被旧通知叫醒。 */
static void test_repeated_wakes_collapse_into_one(void)
{
    const background_job_step_t table[] = {job0};

    begin_scenario();
    jobs_state[0].returns = 100U;

    background_task_wake();
    background_task_wake();
    background_task_wake();
    assert(background_loop_step(table, 1U));
    assert(jobs_state[0].calls == 1U);
    assert(!background_loop_step(table, 1U));
    assert(jobs_state[0].calls == 2U);
}

/** @brief wake 落在后台任务的通知槽和约定的标志位上；任务入口还没绑定时是空操作。 */
static void test_wake_targets_the_bound_task_slot_and_flag(void)
{
    TaskHandle_t other;

    freertos_stub_reset();
    other = freertos_stub_task_create();
    assert(other != NULL);

    background_loop_bind(NULL);
    background_task_wake();
    assert(freertos_stub_notify_calls(other, (UBaseType_t)BACKGROUND_NOTIFY_EVENT) == 0U);
    assert(freertos_stub_notify_calls(xTaskGetCurrentTaskHandle(), (UBaseType_t)BACKGROUND_NOTIFY_EVENT) == 0U);

    background_loop_bind(other);
    background_task_wake();
    assert(freertos_stub_notify_calls(other, (UBaseType_t)BACKGROUND_NOTIFY_EVENT) == 1U);
    assert(freertos_stub_notification_value(other, (UBaseType_t)BACKGROUND_NOTIFY_EVENT) == 1U);
    assert(BACKGROUND_NOTIFY_FLAG_WAKE == 1UL);
}

/**
 * @brief 卷事件叫醒后台任务：任务入口登记订阅后，Flash 或 SD 卷发布卷就绪、卷消失都落在同一个通知槽的 VOLUME 位上，
 *        睡眠（含无限等待）被它叫醒，醒来时这一位被一次收取并清除，下一圈不再被旧事件叫醒。
 */
static void test_volume_event_wakes_the_loop_and_its_flag_is_cleared(void)
{
    const background_job_step_t table[] = {job0};
    const TaskHandle_t task = xTaskGetCurrentTaskHandle();

    begin_scenario();
    jobs_state[0].returns = BACKGROUND_WAIT_FOREVER;
    assert(volume_subscribe(task, (UBaseType_t)BACKGROUND_NOTIFY_EVENT, BACKGROUND_NOTIFY_FLAG_VOLUME));

    (void)volume_publish_ready(VOLUME_ID_FLASH);
    assert(freertos_stub_notification_value(task, (UBaseType_t)BACKGROUND_NOTIFY_EVENT) == BACKGROUND_NOTIFY_FLAG_VOLUME);
    assert(BACKGROUND_NOTIFY_FLAG_VOLUME == 2UL);
    assert(background_loop_step(table, 1U));
    assert(waits_seen.waits == 0U); /* 通知已在，睡眠根本没有开始。 */
    assert(freertos_stub_notification_value(task, (UBaseType_t)BACKGROUND_NOTIFY_EVENT) == 0U); /* 位被清除 */

    (void)volume_publish_gone(VOLUME_ID_SD);
    assert(background_loop_step(table, 1U));
    assert(freertos_stub_notification_value(task, (UBaseType_t)BACKGROUND_NOTIFY_EVENT) == 0U);

    assert(!background_loop_step(table, 1U)); /* 没有新事件：回到无限等待 */
    assert(waits_seen.last_ticks == portMAX_DELAY);
}

/**
 * @brief 运行全部用例。
 * @return 0 表示全部通过（失败由 assert 中止）。
 */
int main(void)
{
    test_every_job_is_called_once_per_lap_in_order();
    test_lap_returns_the_smallest_wait();
    test_wait_ticks_conversion();
    test_step_sleeps_the_smallest_wait_when_nobody_wakes();
    test_step_blocks_forever_when_every_job_waits_for_a_notification();
    test_step_wakes_early_on_notification();
    test_notification_during_the_lap_is_not_lost();
    test_repeated_wakes_collapse_into_one();
    test_wake_targets_the_bound_task_slot_and_flag();
    test_volume_event_wakes_the_loop_and_its_flag_is_cleared();
    puts("background_loop: OK");
    return 0;
}
