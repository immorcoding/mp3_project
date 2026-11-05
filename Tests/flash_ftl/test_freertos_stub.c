/**
 * @file test_freertos_stub.c
 * @brief 共用 FreeRTOS 主机桩的行为：只经 FreeRTOS 公开 API 与桩的脚本化接口观察。
 */

#include "freertos_stub.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/**
 * @brief 自动递增步长为 0 时 tick 停在设定值，设定步长后每次查询前进一步。
 */
static void test_tick_follows_script(void)
{
    freertos_stub_reset();
    freertos_stub_set_tick(100U);
    assert(xTaskGetTickCount() == 100U);
    assert(xTaskGetTickCount() == 100U);

    freertos_stub_set_tick_step(10U);
    assert(xTaskGetTickCount() == 110U);
    assert(xTaskGetTickCount() == 120U);
}

/**
 * @brief 空闲互斥锁可以拿到，持有期间别人拿不到，释放后又可以拿到。
 */
static void test_mutex_is_exclusive_until_given(void)
{
    SemaphoreHandle_t mutex;

    freertos_stub_reset();
    mutex = xSemaphoreCreateMutex();
    assert(mutex != NULL);

    assert(xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE);
    assert(xSemaphoreTake(mutex, 0U) == pdFALSE);
    assert(xSemaphoreGive(mutex) == pdTRUE);
    assert(xSemaphoreTake(mutex, 0U) == pdTRUE);
    assert(xSemaphoreGive(mutex) == pdTRUE);
    /* 没有持有时释放是错误，与真实 FreeRTOS 一致。 */
    assert(xSemaphoreGive(mutex) == pdFALSE);
    vSemaphoreDelete(mutex);
}

/**
 * @brief 拿不到锁时等满超时：tick 前进超时的长度，并计入一次失败等待。
 */
static void test_failed_take_consumes_timeout(void)
{
    SemaphoreHandle_t mutex;
    FreertosStub_SemaphoreStats stats;

    freertos_stub_reset();
    freertos_stub_set_tick(1000U);
    mutex = xSemaphoreCreateMutex();
    assert(xSemaphoreTake(mutex, 0U) == pdTRUE);

    assert(xSemaphoreTake(mutex, pdMS_TO_TICKS(50U)) == pdFALSE);
    assert(xTaskGetTickCount() == 1050U);

    stats = freertos_stub_semaphore_stats(mutex);
    assert(stats.take_calls == 2U);
    assert(stats.take_successes == 1U);
    assert(stats.waits == 1U);
    assert(stats.last_timeout_ticks == 50U);
}

/**
 * @brief 脚本指定接下来 N 次拿锁失败：即使锁空闲也失败，之后恢复正常且不改变持有状态。
 */
static void test_scripted_take_failures_then_recover(void)
{
    SemaphoreHandle_t mutex;

    freertos_stub_reset();
    mutex = xSemaphoreCreateMutex();
    freertos_stub_semaphore_fail_next_takes(mutex, 2U);

    assert(xSemaphoreTake(mutex, 0U) == pdFALSE);
    assert(xSemaphoreTake(mutex, 10U) == pdFALSE);
    assert(xSemaphoreTake(mutex, 0U) == pdTRUE);
    assert(xSemaphoreGive(mutex) == pdTRUE);
}

/**
 * @brief 被测模块在内部创建锁、不交出句柄时，测试经“最近创建的信号量”拿到它来脚本化拿不到。
 */
static void test_last_created_semaphore_reaches_module_internal_lock(void)
{
    static StaticSemaphore_t module_buffer;
    SemaphoreHandle_t pooled;
    SemaphoreHandle_t internal;

    freertos_stub_reset();
    assert(freertos_stub_last_created_semaphore() == NULL);

    pooled = xSemaphoreCreateMutex();
    assert(freertos_stub_last_created_semaphore() == pooled);

    internal = xSemaphoreCreateMutexStatic(&module_buffer);
    assert(freertos_stub_last_created_semaphore() == internal);

    freertos_stub_semaphore_fail_next_takes(freertos_stub_last_created_semaphore(), 1U);
    assert(xSemaphoreTake(internal, 0U) == pdFALSE);
    assert(xSemaphoreTake(internal, 0U) == pdTRUE);
    assert(xSemaphoreGive(internal) == pdTRUE);
}

/**
 * @brief 被测模块有多把内部锁时，测试按创建顺序枚举它们；reset 之后清空列表。
 */
static void test_created_semaphores_are_listed_in_creation_order(void)
{
    static StaticSemaphore_t first_buffer;
    static StaticSemaphore_t second_buffer;
    SemaphoreHandle_t first;
    SemaphoreHandle_t pooled;
    SemaphoreHandle_t second;

    freertos_stub_reset();
    assert(freertos_stub_created_semaphore_count() == 0U);

    first = xSemaphoreCreateMutexStatic(&first_buffer);
    pooled = xSemaphoreCreateBinary();
    second = xSemaphoreCreateMutexStatic(&second_buffer);
    assert(freertos_stub_created_semaphore_count() == 3U);
    assert(freertos_stub_created_semaphore(0U) == first);
    assert(freertos_stub_created_semaphore(1U) == pooled);
    assert(freertos_stub_created_semaphore(2U) == second);
    assert(freertos_stub_created_semaphore(3U) == NULL);

    freertos_stub_reset();
    assert(freertos_stub_created_semaphore_count() == 0U);
}

/**
 * @brief 临界区宏在主机桩里不做事，但可以成对嵌套使用（被测模块在临界区里更新统计）。
 */
static void test_critical_section_macros_are_nestable_no_ops(void)
{
    taskENTER_CRITICAL();
    taskENTER_CRITICAL();
    taskEXIT_CRITICAL();
    taskEXIT_CRITICAL();
}

/** @brief 阻塞钩子的记录。 */
typedef struct
{
    uint32_t calls;
    FreertosStub_Wait last;
    SemaphoreHandle_t give_on_wait;
} HookRecord;

/**
 * @brief 记录等待，并按脚本替“另一个任务”释放信号量。
 * @param[in] wait 等待描述。
 * @param[in,out] context HookRecord。
 */
static void record_and_release(const FreertosStub_Wait *wait, void *context)
{
    HookRecord *record = (HookRecord *)context;

    record->calls++;
    record->last = *wait;
    if (record->give_on_wait != NULL)
    {
        assert(xSemaphoreGive(record->give_on_wait) == pdTRUE);
    }
}

/**
 * @brief 记录等待，并替“中断”向当前任务的等待槽发一个值为 7 的通知。
 * @param[in] wait 等待描述。
 * @param[in,out] context HookRecord。
 */
static void notify_on_wait(const FreertosStub_Wait *wait, void *context)
{
    HookRecord *record = (HookRecord *)context;

    record->calls++;
    record->last = *wait;
    assert(xTaskNotifyIndexed(wait->task, wait->index, 7U, eSetValueWithOverwrite) == pdPASS);
}

/**
 * @brief 二值信号量创建时为空，Give 之后可以 Take 一次，已有信号时再 Give 失败。
 */
static void test_binary_semaphore_starts_empty_and_holds_one_signal(void)
{
    StaticSemaphore_t storage;
    SemaphoreHandle_t gate;
    BaseType_t woken = pdTRUE;

    freertos_stub_reset();
    gate = xSemaphoreCreateBinaryStatic(&storage);
    assert(gate == &storage);

    assert(xSemaphoreTake(gate, 0U) == pdFALSE);
    assert(xSemaphoreGive(gate) == pdTRUE);
    assert(xSemaphoreGive(gate) == pdFALSE);
    assert(xSemaphoreTake(gate, 0U) == pdTRUE);
    assert(xSemaphoreTake(gate, 0U) == pdFALSE);

    assert(xSemaphoreGiveFromISR(gate, &woken) == pdTRUE);
    assert(woken == pdFALSE);
    assert(xSemaphoreTake(gate, 0U) == pdTRUE);
}

/**
 * @brief 关闸时等待：钩子在等待期间打开闸，Take 在超时内成功，tick 不前进。
 */
static void test_wait_on_closed_gate_succeeds_when_hook_opens_it(void)
{
    SemaphoreHandle_t gate;
    HookRecord record = {0};
    FreertosStub_SemaphoreStats stats;

    freertos_stub_reset();
    freertos_stub_set_tick(500U);
    gate = xSemaphoreCreateBinary();
    record.give_on_wait = gate;
    freertos_stub_set_block_hook(record_and_release, &record);

    assert(xSemaphoreTake(gate, 200U) == pdTRUE);
    assert(record.calls == 1U);
    assert(record.last.kind == FREERTOS_STUB_WAIT_SEMAPHORE);
    assert(record.last.semaphore == gate);
    assert(record.last.ticks == 200U);
    assert(xTaskGetTickCount() == 500U);

    stats = freertos_stub_semaphore_stats(gate);
    assert(stats.waits == 1U);
    assert(stats.take_successes == 1U);
}

/**
 * @brief 钩子只在真的要等时被调用：对象可用、或超时为 0 时不调用。
 */
static void test_hook_only_runs_when_a_take_would_block(void)
{
    SemaphoreHandle_t mutex;
    HookRecord record = {0};

    freertos_stub_reset();
    mutex = xSemaphoreCreateMutex();
    freertos_stub_set_block_hook(record_and_release, &record);

    assert(xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE);
    assert(record.calls == 0U);
    assert(xSemaphoreTake(mutex, 0U) == pdFALSE);
    assert(record.calls == 0U);
    assert(xSemaphoreTake(mutex, 10U) == pdFALSE);
    assert(record.calls == 1U);
}

/**
 * @brief 无限等待却没人放行：失败返回并计数，让测试发现真实环境里会死等的路径。
 */
static void test_infinite_wait_nobody_releases_is_counted(void)
{
    SemaphoreHandle_t gate;

    freertos_stub_reset();
    gate = xSemaphoreCreateBinary();
    assert(freertos_stub_unsatisfied_infinite_waits() == 0U);

    assert(xSemaphoreTake(gate, portMAX_DELAY) == pdFALSE);
    assert(freertos_stub_unsatisfied_infinite_waits() == 1U);
    assert(xTaskGetTickCount() == 0U);
}

/**
 * @brief 通知计数：Give 累加，Take 不清零时每次减一并返回减之前的值，清零时一次取空。
 */
static void test_notify_give_take_counts(void)
{
    TaskHandle_t self;

    freertos_stub_reset();
    self = xTaskGetCurrentTaskHandle();
    assert(self != NULL);

    assert(ulTaskNotifyTake(pdFALSE, 0U) == 0U);
    assert(xTaskNotifyGive(self) == pdPASS);
    assert(xTaskNotifyGive(self) == pdPASS);
    assert(freertos_stub_notify_calls(self, 0U) == 2U);

    assert(ulTaskNotifyTake(pdFALSE, 0U) == 2U);
    assert(ulTaskNotifyTake(pdTRUE, 0U) == 1U);
    assert(ulTaskNotifyTake(pdTRUE, 0U) == 0U);
    assert(!freertos_stub_notification_pending(self, 0U));
}

/**
 * @brief 各动作对通知值的作用与真实 FreeRTOS 一致，WithoutOverwrite 在已有通知时失败。
 */
static void test_notify_actions_update_value(void)
{
    TaskHandle_t self;
    uint32_t value = 0U;

    freertos_stub_reset();
    self = xTaskGetCurrentTaskHandle();

    assert(xTaskNotifyIndexed(self, 1U, 0x05U, eSetBits) == pdPASS);
    assert(xTaskNotifyIndexed(self, 1U, 0x30U, eSetBits) == pdPASS);
    assert(freertos_stub_notification_value(self, 1U) == 0x35U);

    assert(xTaskNotifyIndexed(self, 1U, 0x07U, eSetValueWithoutOverwrite) == pdFAIL);
    assert(freertos_stub_notification_value(self, 1U) == 0x35U);
    assert(xTaskNotifyIndexed(self, 1U, 0x07U, eSetValueWithOverwrite) == pdPASS);
    assert(freertos_stub_notification_value(self, 1U) == 0x07U);

    assert(xTaskNotifyIndexed(self, 1U, 0U, eNoAction) == pdPASS);
    assert(freertos_stub_notification_value(self, 1U) == 0x07U);

    assert(xTaskNotifyWaitIndexed(1U, 0U, 0xFFFFFFFFU, &value, 0U) == pdTRUE);
    assert(value == 0x07U);
    /* 取走后没有待处理通知，也没有残留值。 */
    assert(!freertos_stub_notification_pending(self, 1U));
    assert(freertos_stub_notification_value(self, 1U) == 0U);
    assert(xTaskNotifyIndexed(self, 1U, 0x09U, eSetValueWithoutOverwrite) == pdPASS);
}

/**
 * @brief 完成通知的等待：从中断发出的通知被等待者取到；没有通知时按超时失败且不改输出值。
 */
static void test_notify_wait_receives_isr_notification_or_times_out(void)
{
    TaskHandle_t self;
    BaseType_t woken = pdTRUE;
    uint32_t value = 0xDEADBEEFU;

    freertos_stub_reset();
    freertos_stub_set_tick(10U);
    self = xTaskGetCurrentTaskHandle();

    assert(xTaskNotifyWaitIndexed(2U, 0U, 0xFFFFFFFFU, &value, pdMS_TO_TICKS(30U)) == pdFALSE);
    assert(value == 0xDEADBEEFU);
    assert(xTaskGetTickCount() == 40U);

    assert(xTaskNotifyIndexedFromISR(self, 2U, 3U, eSetValueWithOverwrite, &woken) == pdPASS);
    assert(woken == pdFALSE);
    assert(xTaskNotifyWaitIndexed(2U, 0U, 0xFFFFFFFFU, &value, 0U) == pdTRUE);
    assert(value == 3U);
}

/**
 * @brief 清除状态只作用于指定槽；槽之间互不影响。
 */
static void test_notify_state_clear_is_per_index(void)
{
    TaskHandle_t self;

    freertos_stub_reset();
    self = xTaskGetCurrentTaskHandle();
    assert(xTaskNotifyIndexed(self, 0U, 1U, eSetValueWithOverwrite) == pdPASS);
    assert(xTaskNotifyIndexed(self, 3U, 1U, eSetValueWithOverwrite) == pdPASS);

    assert(xTaskNotifyStateClearIndexed(self, 3U) == pdTRUE);
    assert(xTaskNotifyStateClearIndexed(self, 3U) == pdFALSE);
    assert(freertos_stub_notification_pending(self, 0U));
    assert(!freertos_stub_notification_pending(self, 3U));
}

/**
 * @brief 通知发给别的任务时不唤醒当前任务；切换当前任务后能取到发给它的通知。
 */
static void test_notify_targets_the_named_task(void)
{
    TaskHandle_t main_task;
    TaskHandle_t worker;

    freertos_stub_reset();
    main_task = xTaskGetCurrentTaskHandle();
    worker = freertos_stub_task_create();
    assert(worker != NULL && worker != main_task);

    assert(xTaskNotifyGive(worker) == pdPASS);
    assert(ulTaskNotifyTake(pdTRUE, 0U) == 0U);

    freertos_stub_set_current_task(worker);
    assert(xTaskGetCurrentTaskHandle() == worker);
    assert(ulTaskNotifyTake(pdTRUE, 0U) == 1U);
}

/**
 * @brief 等待通知时钩子可以替中断发通知：描述里带当前任务与槽号，等待在超时内成功。
 */
static void test_notify_wait_hook_delivers_notification(void)
{
    TaskHandle_t self;
    HookRecord record = {0};
    uint32_t value = 0U;

    freertos_stub_reset();
    self = xTaskGetCurrentTaskHandle();
    freertos_stub_set_block_hook(notify_on_wait, &record);

    assert(xTaskNotifyWaitIndexed(1U, 0U, 0xFFFFFFFFU, &value, 100U) == pdTRUE);
    assert(value == 7U);
    assert(record.calls == 1U);
    assert(record.last.kind == FREERTOS_STUB_WAIT_NOTIFICATION);
    assert(record.last.task == self);
    assert(record.last.index == 1U);
    assert(record.last.ticks == 100U);
}

/**
 * @brief 中断里 Give 单独计数，且按脚本报告“唤醒了更高优先级任务”；portYIELD_FROM_ISR 只在传入非零时计一次让出。
 */
static void test_give_from_isr_counts_and_reports_woken(void)
{
    StaticSemaphore_t storage;
    SemaphoreHandle_t gate;
    FreertosStub_SemaphoreStats stats;
    BaseType_t woken = pdFALSE;

    freertos_stub_reset();
    gate = xSemaphoreCreateBinaryStatic(&storage);

    /* 默认不唤醒更高优先级任务：传入的标志保持原值语义（桩写入 pdFALSE），不请求让出。 */
    assert(xSemaphoreGiveFromISR(gate, &woken) == pdTRUE);
    assert(woken == pdFALSE);
    portYIELD_FROM_ISR(woken);
    assert(freertos_stub_isr_yield_requests() == 0U);
    assert(xSemaphoreTake(gate, 0U) == pdTRUE);

    /* 脚本让下一次 GiveFromISR 报告唤醒了更高优先级任务。 */
    freertos_stub_set_give_from_isr_woken(pdTRUE);
    assert(xSemaphoreGiveFromISR(gate, &woken) == pdTRUE);
    assert(woken == pdTRUE);
    portYIELD_FROM_ISR(woken);
    assert(freertos_stub_isr_yield_requests() == 1U);

    /* 任务里的 Give 与中断里的 Give 分开计数；Give 失败（已有信号）也算一次调用。 */
    assert(xSemaphoreTake(gate, 0U) == pdTRUE);
    assert(xSemaphoreGive(gate) == pdTRUE);
    assert(xSemaphoreGiveFromISR(gate, NULL) == pdFALSE);
    stats = freertos_stub_semaphore_stats(gate);
    assert(stats.give_calls == 4U);
    assert(stats.give_from_isr_calls == 3U);

    /* reset 清掉脚本与计数。 */
    freertos_stub_reset();
    assert(freertos_stub_isr_yield_requests() == 0U);
    gate = xSemaphoreCreateBinaryStatic(&storage);
    assert(xSemaphoreGiveFromISR(gate, &woken) == pdTRUE);
    assert(woken == pdFALSE);
}

/**
 * @brief vTaskDelay 让 tick 前进给定长度；0 个 tick 不前进。
 */
static void test_task_delay_advances_tick(void)
{
    freertos_stub_reset();
    freertos_stub_set_tick(40U);
    vTaskDelay(1U);
    assert(xTaskGetTickCount() == 41U);
    vTaskDelay(25U);
    assert(xTaskGetTickCount() == 66U);
    vTaskDelay(0U);
    assert(xTaskGetTickCount() == 66U);
}

/** @brief 事件组桩测试里，等待将要发生时替“别的任务”置位的钩子上下文。 */
typedef struct
{
    EventGroupHandle_t group;
    EventBits_t bits_to_set;
    uint32_t calls;
} SetBitsOnWait;

/**
 * @brief 阻塞钩子：事件组的等待将要发生时，别的任务置位。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context SetBitsOnWait。
 */
static void set_bits_on_event_wait(const FreertosStub_Wait *wait, void *context)
{
    SetBitsOnWait *state = (SetBitsOnWait *)context;

    if ((wait->kind == FREERTOS_STUB_WAIT_EVENT_GROUP) && (wait->event_group == state->group))
    {
        state->calls++;
        (void)xEventGroupSetBits(state->group, state->bits_to_set);
    }
}

/**
 * @brief 事件组：置位、清位、读位；等待的位已经置上就立刻返回而不进钩子，不清位时位保留。
 */
static void test_event_group_bits_set_clear_and_wait_without_blocking(void)
{
    StaticEventGroup_t storage;
    EventGroupHandle_t group;
    SetBitsOnWait state;
    FreertosStub_EventGroupStats stats;

    freertos_stub_reset();
    group = xEventGroupCreateStatic(&storage);
    assert(group != NULL);
    assert(freertos_stub_last_created_event_group() == group);
    assert(xEventGroupGetBits(group) == 0U);

    assert((xEventGroupSetBits(group, 0x01U) & 0x01U) != 0U);
    assert((xEventGroupSetBits(group, 0x04U) & 0x05U) == 0x05U);
    state = (SetBitsOnWait){group, 0x02U, 0U};
    freertos_stub_set_block_hook(set_bits_on_event_wait, &state);

    /* 位已经在：立刻返回，不算阻塞，位保留（xClearOnExit 为假）。 */
    assert((xEventGroupWaitBits(group, 0x01U, pdFALSE, pdFALSE, portMAX_DELAY) & 0x01U) != 0U);
    assert(state.calls == 0U);
    assert((xEventGroupGetBits(group) & 0x01U) != 0U);

    /* 清位后再看：位没了。xEventGroupClearBits 返回清之前的值。 */
    assert((xEventGroupClearBits(group, 0x01U) & 0x01U) != 0U);
    assert((xEventGroupGetBits(group) & 0x01U) == 0U);
    assert((xEventGroupGetBits(group) & 0x04U) != 0U);

    stats = freertos_stub_event_group_stats(group);
    assert(stats.wait_calls == 1U);
    assert(stats.blocked_waits == 0U);
    assert(stats.set_calls == 2U);
    assert(stats.clear_calls == 1U);
}

/**
 * @brief 事件组：等待的位没置上时先进钩子，钩子里置位则等待成功；钩子不置位则按超时返回当前位，
 *        无限等待无人置位计入未满足次数。
 */
static void test_event_group_wait_blocks_until_hook_sets_the_bit(void)
{
    StaticEventGroup_t storage;
    EventGroupHandle_t group;
    SetBitsOnWait state;
    FreertosStub_EventGroupStats stats;
    TickType_t before;

    freertos_stub_reset();
    group = xEventGroupCreateStatic(&storage);
    state = (SetBitsOnWait){group, 0x01U, 0U};
    freertos_stub_set_block_hook(set_bits_on_event_wait, &state);

    assert((xEventGroupWaitBits(group, 0x01U, pdFALSE, pdFALSE, portMAX_DELAY) & 0x01U) != 0U);
    assert(state.calls == 1U);
    stats = freertos_stub_event_group_stats(group);
    assert(stats.blocked_waits == 1U);

    /* 钩子不置位（位已被清）：有限等待让 tick 前进超时长度并返回没有该位的当前值。 */
    assert((xEventGroupClearBits(group, 0x01U) & 0x01U) != 0U);
    freertos_stub_set_block_hook(NULL, NULL);
    freertos_stub_set_tick(500U);
    before = xTaskGetTickCount();
    assert((xEventGroupWaitBits(group, 0x01U, pdFALSE, pdFALSE, 30U) & 0x01U) == 0U);
    assert((xTaskGetTickCount() - before) == 30U);

    /* 超时 0 不阻塞也不进钩子；无限等待无人置位计入未满足次数。 */
    assert((xEventGroupWaitBits(group, 0x01U, pdFALSE, pdFALSE, 0U) & 0x01U) == 0U);
    assert(freertos_stub_unsatisfied_infinite_waits() == 0U);
    assert((xEventGroupWaitBits(group, 0x01U, pdFALSE, pdFALSE, portMAX_DELAY) & 0x01U) == 0U);
    assert(freertos_stub_unsatisfied_infinite_waits() == 1U);
}

/**
 * @brief 运行全部桩行为用例。
 * @return 全部断言通过为零，否则终止进程。
 */
int main(void)
{
    test_event_group_bits_set_clear_and_wait_without_blocking();
    test_event_group_wait_blocks_until_hook_sets_the_bit();
    test_tick_follows_script();
    test_mutex_is_exclusive_until_given();
    test_failed_take_consumes_timeout();
    test_scripted_take_failures_then_recover();
    test_last_created_semaphore_reaches_module_internal_lock();
    test_created_semaphores_are_listed_in_creation_order();
    test_critical_section_macros_are_nestable_no_ops();
    test_binary_semaphore_starts_empty_and_holds_one_signal();
    test_wait_on_closed_gate_succeeds_when_hook_opens_it();
    test_hook_only_runs_when_a_take_would_block();
    test_infinite_wait_nobody_releases_is_counted();
    test_notify_give_take_counts();
    test_notify_actions_update_value();
    test_notify_wait_receives_isr_notification_or_times_out();
    test_notify_state_clear_is_per_index();
    test_notify_targets_the_named_task();
    test_notify_wait_hook_delivers_notification();
    test_give_from_isr_counts_and_reports_woken();
    test_task_delay_advances_tick();
    puts("freertos_stub tests passed");
    return 0;
}
