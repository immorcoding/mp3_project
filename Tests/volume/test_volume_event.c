/**
 ******************************************************************************
 * @file    test_volume_event.c
 * @brief   卷事件的主机测试：序号递增、先更新再叫醒、快速拔插跳 2、晚登记不漏、订阅者上限。
 *
 * @details
 *          只经 `volume_event.h` 的公开函数观察，叫醒用共用 FreeRTOS 桩的假任务和通知计数。模块的状态是静态的，
 *          没有（也不该有）测试用的复位入口，所以每个用例在独立进程里运行：命令行第一个参数选用例，
 *          CMake 为每个用例注册一个 CTest。主机测试是单线程的，通过不证明真实并发下订阅者与发布者交错的正确性
 *          （那靠“登记后先比对一次序号”的设计和第 3 次上板），只证明这个设计里每一步的分支和顺序。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos_stub.h"
#include "APP/tasks/volume/volume_event.h"
#include "APP/tasks/volume/volume_task_config.h"

#define TEST_NOTIFY_INDEX 2U
#define TEST_FLAG_A 0x10U
#define TEST_FLAG_B 0x20U

/**
 * @brief 开机状态：两个卷都没挂，序号为 0，互不相干。
 */
static void test_initial_state_is_unmounted_with_sequence_zero(void)
{
    Volume_StatusTypeDef sd = volume_status(VOLUME_ID_SD);
    Volume_StatusTypeDef flash = volume_status(VOLUME_ID_FLASH);

    assert(!sd.Mounted);
    assert(sd.Sequence == 0U);
    assert(!flash.Mounted);
    assert(flash.Sequence == 0U);
}

/**
 * @brief 每次发布序号加一：就绪后挂载位为真，消失后为假；两个卷各有各的字，互不影响。
 */
static void test_sequence_increments_per_event_and_volumes_are_independent(void)
{
    assert(volume_publish_ready(VOLUME_ID_SD) == 1U);
    assert(volume_status(VOLUME_ID_SD).Mounted);
    assert(volume_status(VOLUME_ID_SD).Sequence == 1U);

    assert(volume_publish_gone(VOLUME_ID_SD) == 2U);
    assert(!volume_status(VOLUME_ID_SD).Mounted);
    assert(volume_status(VOLUME_ID_SD).Sequence == 2U);

    /* Flash 的字没动过。 */
    assert(volume_status(VOLUME_ID_FLASH).Sequence == 0U);
    assert(!volume_status(VOLUME_ID_FLASH).Mounted);

    assert(volume_publish_ready(VOLUME_ID_FLASH) == 1U);
    assert(volume_status(VOLUME_ID_FLASH).Mounted);
    assert(volume_status(VOLUME_ID_SD).Sequence == 2U);
    assert(!volume_status(VOLUME_ID_SD).Mounted);
}

/**
 * @brief 订阅者被叫醒：通知落在它登记的槽和标志位上，其他槽不受影响；两个卷的事件都叫醒同一个订阅者。
 */
static void test_subscriber_is_woken_on_its_registered_slot_and_flag(void)
{
    TaskHandle_t subscriber;
    TaskHandle_t bystander;

    freertos_stub_reset();
    subscriber = freertos_stub_task_create();
    bystander = freertos_stub_task_create();
    assert(volume_subscribe(subscriber, TEST_NOTIFY_INDEX, TEST_FLAG_A));

    (void)volume_publish_ready(VOLUME_ID_SD);

    assert(freertos_stub_notify_calls(subscriber, TEST_NOTIFY_INDEX) == 1U);
    assert(freertos_stub_notification_value(subscriber, TEST_NOTIFY_INDEX) == TEST_FLAG_A);
    assert(freertos_stub_notify_calls(subscriber, 0U) == 0U);
    assert(freertos_stub_notify_calls(bystander, TEST_NOTIFY_INDEX) == 0U);

    (void)volume_publish_ready(VOLUME_ID_FLASH);
    assert(freertos_stub_notify_calls(subscriber, TEST_NOTIFY_INDEX) == 2U);
}

/** @brief 叫醒钩子的记录：每次叫醒时读到的两个卷的状态。 */
typedef struct
{
    uint32_t calls;
    Volume_StatusTypeDef sd_seen;
    Volume_StatusTypeDef flash_seen;
} WakeRecord;

/**
 * @brief 通知钩子：叫醒发出的那一刻读卷状态，证明状态已经先更新。
 * @param[in] task 目标任务。
 * @param[in] index 槽号。
 * @param[in] value 通知值。
 * @param[in,out] context 指向 WakeRecord。
 */
static void record_state_at_wake(TaskHandle_t task, UBaseType_t index, uint32_t value, void *context)
{
    WakeRecord *record = (WakeRecord *)context;

    (void)task;
    (void)index;
    (void)value;
    record->calls++;
    record->sd_seen = volume_status(VOLUME_ID_SD);
    record->flash_seen = volume_status(VOLUME_ID_FLASH);
}

/**
 * @brief 先更新再叫醒：订阅者被叫醒的那一刻，卷状态字里已经是新序号和新挂载位；
 *        一次事件叫醒每个订阅者各一次，没有在更新之前叫醒的。
 */
static void test_state_is_updated_before_any_subscriber_is_woken(void)
{
    WakeRecord record = {0};
    TaskHandle_t first;
    TaskHandle_t second;

    freertos_stub_reset();
    first = freertos_stub_task_create();
    second = freertos_stub_task_create();
    assert(volume_subscribe(first, TEST_NOTIFY_INDEX, TEST_FLAG_A));
    assert(volume_subscribe(second, 0U, TEST_FLAG_B));
    freertos_stub_set_notify_hook(record_state_at_wake, &record);

    (void)volume_publish_ready(VOLUME_ID_SD);

    assert(record.calls == 2U);
    assert(record.sd_seen.Sequence == 1U);
    assert(record.sd_seen.Mounted);
    assert(freertos_stub_notify_calls(first, TEST_NOTIFY_INDEX) == 1U);
    assert(freertos_stub_notify_calls(second, 0U) == 1U);

    record.calls = 0U;
    (void)volume_publish_gone(VOLUME_ID_SD);
    assert(record.calls == 2U);
    assert(record.sd_seen.Sequence == 2U);
    assert(!record.sd_seen.Mounted);

    /* 另一个卷的事件：该卷的字先更新，SD 的字原样。 */
    record.calls = 0U;
    (void)volume_publish_ready(VOLUME_ID_FLASH);
    assert(record.calls == 2U);
    assert(record.flash_seen.Sequence == 1U);
    assert(record.flash_seen.Mounted);
    assert(record.sd_seen.Sequence == 2U);
}

/**
 * @brief 快速拔插（换卡）：消失、就绪各发一次，序号跳 2，挂载位仍为真。订阅者按“旧的全部作废，再看现状”处理：
 *        它上次见到的序号与现在不同，就够判断“变过”；叫醒是两次，但通知位合并，订阅者醒一次就能看到最终状态。
 */
static void test_card_swap_jumps_the_sequence_by_two_and_stays_mounted(void)
{
    TaskHandle_t subscriber;
    uint32_t seen;

    freertos_stub_reset();
    subscriber = freertos_stub_task_create();
    assert(volume_subscribe(subscriber, TEST_NOTIFY_INDEX, TEST_FLAG_A));
    (void)volume_publish_ready(VOLUME_ID_SD);
    seen = volume_status(VOLUME_ID_SD).Sequence;
    assert(seen == 1U);

    (void)volume_publish_gone(VOLUME_ID_SD);
    (void)volume_publish_ready(VOLUME_ID_SD);

    assert(volume_status(VOLUME_ID_SD).Mounted);
    assert(volume_status(VOLUME_ID_SD).Sequence == seen + 2U);
    assert(freertos_stub_notify_calls(subscriber, TEST_NOTIFY_INDEX) == 3U);
    assert(freertos_stub_notification_value(subscriber, TEST_NOTIFY_INDEX) == TEST_FLAG_A);
}

/**
 * @brief 晚登记不漏：订阅者在事件发布之后才登记，登记不会补叫醒（旧事件不重放），
 *        但状态字已经是最新的，登记后先比对一次序号就能看到“卷已就绪”；登记之后的事件照常叫醒。
 */
static void test_late_subscriber_sees_earlier_events_by_comparing_the_sequence(void)
{
    TaskHandle_t late;
    uint32_t last_seen = 0U;

    freertos_stub_reset();
    late = freertos_stub_task_create();
    (void)volume_publish_ready(VOLUME_ID_SD);
    (void)volume_publish_gone(VOLUME_ID_SD);
    (void)volume_publish_ready(VOLUME_ID_SD);

    assert(volume_subscribe(late, TEST_NOTIFY_INDEX, TEST_FLAG_A));
    assert(freertos_stub_notify_calls(late, TEST_NOTIFY_INDEX) == 0U);

    /* 登记后的第一次比对：序号与 0 不同，卷现在是挂着的。 */
    assert(volume_status(VOLUME_ID_SD).Sequence != last_seen);
    assert(volume_status(VOLUME_ID_SD).Mounted);
    assert(volume_status(VOLUME_ID_SD).Sequence == 3U);
    last_seen = volume_status(VOLUME_ID_SD).Sequence;

    (void)volume_publish_gone(VOLUME_ID_SD);
    assert(freertos_stub_notify_calls(late, TEST_NOTIFY_INDEX) == 1U);
    assert(volume_status(VOLUME_ID_SD).Sequence != last_seen);
}

/**
 * @brief 订阅者上限是配置常量：恰好上限个都登记成功，再多一个返回假且不影响已登记的；
 *        被拒绝的任务不会被叫醒，已登记的每个仍然各叫醒一次。
 */
static void test_subscriber_limit_rejects_the_extra_one_without_disturbing_the_others(void)
{
    TaskHandle_t subscribers[VOLUME_SUBSCRIBER_MAX];
    TaskHandle_t extra;

    freertos_stub_reset();
    for (uint32_t index = 0U; index < VOLUME_SUBSCRIBER_MAX; index++)
    {
        subscribers[index] = freertos_stub_task_create();
        assert(volume_subscribe(subscribers[index], TEST_NOTIFY_INDEX, TEST_FLAG_A));
    }
    extra = freertos_stub_task_create();
    assert(!volume_subscribe(extra, TEST_NOTIFY_INDEX, TEST_FLAG_A));

    (void)volume_publish_ready(VOLUME_ID_SD);

    for (uint32_t index = 0U; index < VOLUME_SUBSCRIBER_MAX; index++)
    {
        assert(freertos_stub_notify_calls(subscribers[index], TEST_NOTIFY_INDEX) == 1U);
    }
    assert(freertos_stub_notify_calls(extra, TEST_NOTIFY_INDEX) == 0U);
}

/**
 * @brief 同一个任务、槽、标志位重复登记是幂等的：返回真，不占第二个位置，一次事件只叫醒一次；
 *        空任务句柄登记失败，同一个任务用不同槽登记算两个订阅者。
 */
static void test_duplicate_subscription_is_idempotent_and_null_task_is_rejected(void)
{
    TaskHandle_t subscriber;

    freertos_stub_reset();
    subscriber = freertos_stub_task_create();

    assert(!volume_subscribe(NULL, TEST_NOTIFY_INDEX, TEST_FLAG_A));
    assert(volume_subscribe(subscriber, TEST_NOTIFY_INDEX, TEST_FLAG_A));
    assert(volume_subscribe(subscriber, TEST_NOTIFY_INDEX, TEST_FLAG_A));
    assert(volume_subscribe(subscriber, 0U, TEST_FLAG_B));

    (void)volume_publish_ready(VOLUME_ID_SD);

    assert(freertos_stub_notify_calls(subscriber, TEST_NOTIFY_INDEX) == 1U);
    assert(freertos_stub_notify_calls(subscriber, 0U) == 1U);
}

/**
 * @brief 按命令行参数运行一个用例。
 * @param[in] argc 参数个数。
 * @param[in] argv 参数，argv[1] 是用例名。
 * @return 通过为 0，断言失败时进程中止；用例名不认识为 2。
 */
int main(int argc, char **argv)
{
    const char *name = (argc > 1) ? argv[1] : "";

    if (strcmp(name, "initial_state") == 0)
    {
        test_initial_state_is_unmounted_with_sequence_zero();
    }
    else if (strcmp(name, "sequence") == 0)
    {
        test_sequence_increments_per_event_and_volumes_are_independent();
    }
    else if (strcmp(name, "wake_slot") == 0)
    {
        test_subscriber_is_woken_on_its_registered_slot_and_flag();
    }
    else if (strcmp(name, "update_before_wake") == 0)
    {
        test_state_is_updated_before_any_subscriber_is_woken();
    }
    else if (strcmp(name, "swap_jumps_two") == 0)
    {
        test_card_swap_jumps_the_sequence_by_two_and_stays_mounted();
    }
    else if (strcmp(name, "late_subscriber") == 0)
    {
        test_late_subscriber_sees_earlier_events_by_comparing_the_sequence();
    }
    else if (strcmp(name, "subscriber_limit") == 0)
    {
        test_subscriber_limit_rejects_the_extra_one_without_disturbing_the_others();
    }
    else if (strcmp(name, "duplicate_subscription") == 0)
    {
        test_duplicate_subscription_is_idempotent_and_null_task_is_rejected();
    }
    else
    {
        fprintf(stderr, "unknown case '%s'\n", name);
        return 2;
    }

    printf("volume_event %s: passed\n", name);
    return 0;
}
