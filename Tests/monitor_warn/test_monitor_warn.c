/**
 ******************************************************************************
 * @file    test_monitor_warn.c
 * @brief   Monitor 未登记任务告警去重记录的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/monitor/monitor_task_config.h"
#include "APP/tasks/monitor/monitor_warn.h"

/**
 * @brief 同一任务名只在第一次询问时需要告警。
 */
static void test_same_name_warns_only_once(void)
{
    monitor_warn_record_t record;

    monitor_warn_record_init(&record);

    assert(monitor_warn_should_report(&record, "Foo Task"));
    assert(!monitor_warn_should_report(&record, "Foo Task"));
    assert(!monitor_warn_should_report(&record, "Foo Task"));
}

/**
 * @brief 不同任务名各自告警一次，互不抑制。
 */
static void test_distinct_names_each_warn_once(void)
{
    monitor_warn_record_t record;

    monitor_warn_record_init(&record);

    assert(monitor_warn_should_report(&record, "A"));
    assert(monitor_warn_should_report(&record, "B"));
    assert(!monitor_warn_should_report(&record, "A"));
    assert(!monitor_warn_should_report(&record, "B"));
}

/**
 * @brief 记录写满后新名字保持沉默且不越界，已记录名字仍保持去重。
 */
static void test_full_record_stays_silent_for_new_names(void)
{
    monitor_warn_record_t record;
    char name[MONITOR_WARNED_NAME_LENGTH];

    monitor_warn_record_init(&record);

    for (unsigned index = 0U; index < MONITOR_WARNED_TASK_MAX; index++)
    {
        (void)snprintf(name, sizeof(name), "T%u", index);
        assert(monitor_warn_should_report(&record, name));
    }

    assert(!monitor_warn_should_report(&record, "Overflow"));
    assert(!monitor_warn_should_report(&record, "Overflow"));
    assert(!monitor_warn_should_report(&record, "T0"));
}

/**
 * @brief 超过记录长度的名字被安全截断，同一长名字仍只告警一次。
 */
static void test_long_name_is_truncated_safely(void)
{
    monitor_warn_record_t record;
    const char *long_name = "ThisTaskNameIsFarLongerThanTheRecordSlot";

    monitor_warn_record_init(&record);

    assert(monitor_warn_should_report(&record, long_name));
    assert(!monitor_warn_should_report(&record, long_name));
}

/**
 * @brief 空指针与空名字不告警，也不占用记录。
 */
static void test_null_and_empty_names_are_ignored(void)
{
    monitor_warn_record_t record;

    monitor_warn_record_init(&record);

    assert(!monitor_warn_should_report(&record, NULL));
    assert(!monitor_warn_should_report(NULL, "X"));
    assert(!monitor_warn_should_report(&record, ""));
    assert(monitor_warn_should_report(&record, "X"));
}

/**
 * @brief 主机测试入口。
 * @retval 0 全部断言通过。
 */
int main(void)
{
    test_same_name_warns_only_once();
    test_distinct_names_each_warn_once();
    test_full_record_stays_silent_for_new_names();
    test_long_name_is_truncated_safely();
    test_null_and_empty_names_are_ignored();

    puts("monitor_warn tests passed");
    return 0;
}
