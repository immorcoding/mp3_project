/**
 * @file freertos_stub.c
 * @brief 主机共用 FreeRTOS 桩的实现：互斥锁、二值信号量与 tick。
 */

#include "freertos_stub.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STUB_SEMAPHORE_POOL_SIZE 16U
#define STUB_CREATED_LOG_SIZE 32U
/* 同一串无限等待连续这么多次都没人放行，就当作死锁：被测模块的“等到拿到为止”重试循环否则会空转到测试超时。 */
#define STUB_DEADLOCK_GUARD_WAITS 100000U
#define STUB_TASK_POOL_SIZE 8U
#define STUB_NOTIFY_SLOTS ((UBaseType_t)configTASK_NOTIFICATION_ARRAY_ENTRIES)

typedef enum
{
    STUB_SEMAPHORE_MUTEX = 1,
    STUB_SEMAPHORE_BINARY
} StubSemaphoreKind;

/** @brief 放进 StaticSemaphore_t 缓冲区的真实状态。 */
typedef struct
{
    StubSemaphoreKind kind;
    uint32_t count;
    uint32_t fail_takes;
    FreertosStub_SemaphoreStats stats;
} StubSemaphore;

_Static_assert(sizeof(StubSemaphore) <= sizeof(StaticSemaphore_t), "StaticSemaphore_t 放不下桩状态");

/** @brief 放进 StaticEventGroup_t 缓冲区的真实状态。 */
typedef struct
{
    EventBits_t bits;
    FreertosStub_EventGroupStats stats;
} StubEventGroup;

_Static_assert(sizeof(StubEventGroup) <= sizeof(StaticEventGroup_t), "StaticEventGroup_t 放不下桩状态");

/** @brief 假任务：每个通知槽一个值、一个待处理标志和一个通知计数。 */
struct HostStubTask
{
    uint32_t value[STUB_NOTIFY_SLOTS];
    bool pending[STUB_NOTIFY_SLOTS];
    uint32_t calls[STUB_NOTIFY_SLOTS];
};

static struct HostStubTask task_pool[STUB_TASK_POOL_SIZE];
static uint32_t task_count;
static TaskHandle_t current_task;
static StaticSemaphore_t semaphore_pool[STUB_SEMAPHORE_POOL_SIZE];
static bool semaphore_in_use[STUB_SEMAPHORE_POOL_SIZE];
static SemaphoreHandle_t last_created_semaphore;
static EventGroupHandle_t last_created_event_group;
static SemaphoreHandle_t created_semaphores[STUB_CREATED_LOG_SIZE];
static uint32_t created_semaphore_count;
static uint32_t consecutive_unsatisfied_infinite_waits;
static TickType_t current_tick;
static TickType_t tick_step;
static FreertosStub_BlockHook block_hook;
static void *block_hook_context;
static FreertosStub_NotifyHook notify_hook;
static void *notify_hook_context;
static uint32_t infinite_waits_unsatisfied;
static BaseType_t give_from_isr_woken;
static uint32_t isr_yield_requests;

/**
 * @brief 取得句柄背后的桩状态。
 * @param[in] semaphore 信号量句柄。
 * @return 桩状态指针。
 */
static StubSemaphore *state_of(SemaphoreHandle_t semaphore)
{
    assert(semaphore != NULL);
    return (StubSemaphore *)(void *)semaphore;
}

/**
 * @brief 在给定缓冲区里建立一个信号量。
 * @param[out] buffer 缓冲区。
 * @param[in] kind 种类。
 * @param[in] initial_count 初始计数。
 * @return 句柄。
 */
static SemaphoreHandle_t init_semaphore(StaticSemaphore_t *buffer, StubSemaphoreKind kind, uint32_t initial_count)
{
    StubSemaphore *state = (StubSemaphore *)(void *)buffer;

    assert(buffer != NULL);
    memset(buffer, 0, sizeof(*buffer));
    state->kind = kind;
    state->count = initial_count;
    last_created_semaphore = buffer;
    if (created_semaphore_count < STUB_CREATED_LOG_SIZE)
    {
        created_semaphores[created_semaphore_count++] = buffer;
    }
    return buffer;
}

/**
 * @brief 从池里分配一个信号量。
 * @param[in] kind 种类。
 * @param[in] initial_count 初始计数。
 * @return 句柄；池满返回 NULL。
 */
static SemaphoreHandle_t create_from_pool(StubSemaphoreKind kind, uint32_t initial_count)
{
    for (uint32_t i = 0U; i < STUB_SEMAPHORE_POOL_SIZE; i++)
    {
        if (!semaphore_in_use[i])
        {
            semaphore_in_use[i] = true;
            return init_semaphore(&semaphore_pool[i], kind, initial_count);
        }
    }
    return NULL;
}

/**
 * @brief 产品 configASSERT 失败时的出口：打印文件和行号后 abort()。
 * @param[in] file 断言所在文件。
 * @param[in] line 断言所在行。
 * @note  真实固件里这里是不可恢复的停机；主机测试用 SIGABRT 处理函数捕获以验证断言触发。
 */
void vAssertCalled(const char *file, uint32_t line)
{
    (void)fprintf(stderr, "configASSERT failed: %s:%lu\n", file, (unsigned long)line);
    abort();
}

/**
 * @brief 丢弃所有对象与脚本：tick 与步长归零、钩子取消，并重建默认的主任务。
 */
void freertos_stub_reset(void)
{
    memset(semaphore_pool, 0, sizeof(semaphore_pool));
    memset(semaphore_in_use, 0, sizeof(semaphore_in_use));
    last_created_semaphore = NULL;
    last_created_event_group = NULL;
    memset(created_semaphores, 0, sizeof(created_semaphores));
    created_semaphore_count = 0U;
    consecutive_unsatisfied_infinite_waits = 0U;
    current_tick = 0U;
    tick_step = 0U;
    block_hook = NULL;
    block_hook_context = NULL;
    notify_hook = NULL;
    notify_hook_context = NULL;
    infinite_waits_unsatisfied = 0U;
    give_from_isr_woken = pdFALSE;
    isr_yield_requests = 0U;
    memset(task_pool, 0, sizeof(task_pool));
    task_count = 0U;
    current_task = freertos_stub_task_create();
}

/**
 * @brief 新建一个假任务。
 * @return 任务句柄；任务池用尽返回 NULL。
 */
TaskHandle_t freertos_stub_task_create(void)
{
    if (task_count >= STUB_TASK_POOL_SIZE)
    {
        return NULL;
    }
    return &task_pool[task_count++];
}

/**
 * @brief 指定当前任务。
 * @param[in] task 任务句柄，不能为 NULL。
 */
void freertos_stub_set_current_task(TaskHandle_t task)
{
    assert(task != NULL);
    current_task = task;
}

/**
 * @brief 返回当前任务。
 * @return 当前任务句柄。
 */
TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return current_task;
}

/**
 * @brief 安装或取消阻塞钩子。
 * @param[in] hook 钩子；NULL 表示取消。
 * @param[in,out] context 传给钩子的上下文。
 */
void freertos_stub_set_block_hook(FreertosStub_BlockHook hook, void *context)
{
    block_hook = hook;
    block_hook_context = context;
}

/**
 * @brief 安装通知钩子；传 NULL 取消。
 * @param[in] hook 钩子函数。
 * @param[in,out] context 传给钩子的上下文。
 */
void freertos_stub_set_notify_hook(FreertosStub_NotifyHook hook, void *context)
{
    notify_hook = hook;
    notify_hook_context = context;
}

/**
 * @brief 读取无限等待无人放行的次数。
 * @return 累计次数。
 */
uint32_t freertos_stub_unsatisfied_infinite_waits(void)
{
    return infinite_waits_unsatisfied;
}

/**
 * @brief 让一次没等到的等待过期：有限超时让 tick 前进，无限超时计入未满足次数。
 * @param[in] ticks 调用者给的超时。
 */
static void expire_wait(TickType_t ticks)
{
    if (ticks == portMAX_DELAY)
    {
        infinite_waits_unsatisfied++;
        if (++consecutive_unsatisfied_infinite_waits >= STUB_DEADLOCK_GUARD_WAITS)
        {
            (void)fprintf(stderr, "freertos_stub: infinite wait never released (deadlock in the code under test)\n");
            abort();
        }
    }
    else
    {
        current_tick += ticks;
    }
}

/**
 * @brief 调用阻塞钩子（若已安装）。
 * @param[in] wait 将要进行的等待。
 */
static void run_block_hook(const FreertosStub_Wait *wait)
{
    if (block_hook != NULL)
    {
        block_hook(wait, block_hook_context);
    }
}

/**
 * @brief 设定当前 tick。
 * @param[in] tick 新的 tick 值。
 */
void freertos_stub_set_tick(TickType_t tick)
{
    current_tick = tick;
}

/**
 * @brief 设定每次查询 tick 前自动前进的步长。
 * @param[in] step 步长，0 表示不自动前进。
 */
void freertos_stub_set_tick_step(TickType_t step)
{
    tick_step = step;
}

/**
 * @brief 返回 tick：先按步长前进再返回。
 * @return 当前 tick。
 */
TickType_t xTaskGetTickCount(void)
{
    current_tick += tick_step;
    return current_tick;
}

/**
 * @brief 让之后的 GiveFromISR 报告给定的 higher_priority_task_woken。
 * @param[in] woken pdTRUE 或 pdFALSE。
 */
void freertos_stub_set_give_from_isr_woken(BaseType_t woken)
{
    give_from_isr_woken = woken;
}

/**
 * @brief 读取 portYIELD_FROM_ISR 以非零标志被调用的次数。
 * @return 累计次数。
 */
uint32_t freertos_stub_isr_yield_requests(void)
{
    return isr_yield_requests;
}

/**
 * @brief portYIELD_FROM_ISR 的桩实现：只记录非零请求，不让出。
 * @param[in] higher_priority_task_woken 回调传入的标志。
 */
void vStubPortYieldFromISR(BaseType_t higher_priority_task_woken)
{
    if (higher_priority_task_woken != pdFALSE)
    {
        isr_yield_requests++;
    }
}

/**
 * @brief 让当前任务延时：主机桩没有调度，只让 tick 前进给定长度。
 * @param[in] ticks_to_delay 延时的 tick 数，0 表示不前进。
 */
void vTaskDelay(TickType_t ticks_to_delay)
{
    current_tick += ticks_to_delay;
}

/**
 * @brief 读取信号量的调用统计。
 * @param[in] semaphore 信号量句柄。
 * @return 统计快照。
 */
FreertosStub_SemaphoreStats freertos_stub_semaphore_stats(SemaphoreHandle_t semaphore)
{
    return state_of(semaphore)->stats;
}

/**
 * @brief 最近一次创建（动态或静态）的信号量。
 * @return 句柄；reset 之后还没创建过则为 NULL。
 */
SemaphoreHandle_t freertos_stub_last_created_semaphore(void)
{
    return last_created_semaphore;
}

/**
 * @brief 自 reset 以来创建的信号量个数。
 * @return 个数。
 */
uint32_t freertos_stub_created_semaphore_count(void)
{
    return created_semaphore_count;
}

/**
 * @brief 按创建顺序取信号量。
 * @param[in] index 从 0 起的序号。
 * @return 句柄；超出个数为 NULL。
 */
SemaphoreHandle_t freertos_stub_created_semaphore(uint32_t index)
{
    return (index < created_semaphore_count) ? created_semaphores[index] : NULL;
}

/**
 * @brief 让信号量接下来 N 次拿取失败。
 * @param[in] semaphore 信号量句柄。
 * @param[in] count 失败次数。
 */
void freertos_stub_semaphore_fail_next_takes(SemaphoreHandle_t semaphore, uint32_t count)
{
    state_of(semaphore)->fail_takes = count;
}

/**
 * @brief 从池里创建互斥锁，初始可用。
 * @return 句柄；池满返回 NULL。
 */
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return create_from_pool(STUB_SEMAPHORE_MUTEX, 1U);
}

/**
 * @brief 在调用者缓冲区里创建互斥锁，初始可用。
 * @param[out] buffer 缓冲区。
 * @return 句柄，即缓冲区地址。
 */
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer)
{
    return init_semaphore(buffer, STUB_SEMAPHORE_MUTEX, 1U);
}

/**
 * @brief 从池里创建二值信号量，初始为空。
 * @return 句柄；池满返回 NULL。
 */
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return create_from_pool(STUB_SEMAPHORE_BINARY, 0U);
}

/**
 * @brief 在调用者缓冲区里创建二值信号量，初始为空。
 * @param[out] buffer 缓冲区。
 * @return 句柄，即缓冲区地址。
 */
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *buffer)
{
    return init_semaphore(buffer, STUB_SEMAPHORE_BINARY, 0U);
}

/**
 * @brief 删除信号量，池里的位置可再次分配。
 * @param[in] semaphore 信号量句柄。
 */
void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    ptrdiff_t index = semaphore - semaphore_pool;

    if ((index >= 0) && (index < (ptrdiff_t)STUB_SEMAPHORE_POOL_SIZE))
    {
        semaphore_in_use[index] = false;
    }
    memset(semaphore, 0, sizeof(*semaphore));
}

/**
 * @brief 拿取信号量；拿不到时先调阻塞钩子，仍拿不到按超时失败。
 * @param[in] semaphore 信号量句柄。
 * @param[in] ticks_to_wait 超时，0 表示不等，portMAX_DELAY 表示无限。
 * @retval pdTRUE 拿到。
 * @retval pdFALSE 没拿到。
 */
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait)
{
    StubSemaphore *state = state_of(semaphore);
    bool scripted_failure = state->fail_takes > 0U;

    state->stats.take_calls++;
    state->stats.last_timeout_ticks = ticks_to_wait;
    if (scripted_failure)
    {
        state->fail_takes--;
    }
    else if (state->count == 0U && ticks_to_wait != 0U)
    {
        FreertosStub_Wait wait = {FREERTOS_STUB_WAIT_SEMAPHORE, semaphore, NULL, 0U, ticks_to_wait, NULL};

        state->stats.waits++;
        run_block_hook(&wait);
    }

    if (!scripted_failure && state->count > 0U)
    {
        state->count--;
        state->stats.take_successes++;
        consecutive_unsatisfied_infinite_waits = 0U;
        return pdTRUE;
    }
    if (scripted_failure && ticks_to_wait != 0U)
    {
        state->stats.waits++;
    }
    if (ticks_to_wait != 0U)
    {
        expire_wait(ticks_to_wait);
    }
    return pdFALSE;
}

/**
 * @brief 释放信号量；已经可用时失败。
 * @param[in] semaphore 信号量句柄。
 * @retval pdTRUE 释放成功。
 * @retval pdFALSE 信号量本来就可用。
 */
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    StubSemaphore *state = state_of(semaphore);

    state->stats.give_calls++;
    /* 互斥锁和二值信号量最多一个计数：已可用时再释放失败。 */
    if (state->count >= 1U)
    {
        return pdFALSE;
    }
    state->count = 1U;
    state->stats.give_successes++;
    consecutive_unsatisfied_infinite_waits = 0U;
    return pdTRUE;
}

/**
 * @brief 中断里释放信号量；主机桩没有调度，不真的让出，只按脚本报告是否唤醒了更高优先级任务。
 * @param[in] semaphore 信号量句柄。
 * @param[out] higher_priority_task_woken 可为 NULL，写入脚本值（默认 pdFALSE）。
 * @retval pdTRUE 释放成功。
 * @retval pdFALSE 信号量本来就可用。
 */
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *higher_priority_task_woken)
{
    state_of(semaphore)->stats.give_from_isr_calls++;
    if (higher_priority_task_woken != NULL)
    {
        *higher_priority_task_woken = give_from_isr_woken;
    }
    return xSemaphoreGive(semaphore);
}

/**
 * @brief 校验任务与槽号并返回槽的下标。
 * @param[in] task 任务句柄。
 * @param[in] index 槽号。
 * @return 槽号本身（已校验）。
 */
static UBaseType_t checked_slot(TaskHandle_t task, UBaseType_t index)
{
    assert(task != NULL);
    assert(index < STUB_NOTIFY_SLOTS);
    return index;
}

/**
 * @brief 读取通知槽的值。
 * @param[in] task 任务句柄。
 * @param[in] index 槽号。
 * @return 通知值。
 */
uint32_t freertos_stub_notification_value(TaskHandle_t task, UBaseType_t index)
{
    return task->value[checked_slot(task, index)];
}

/**
 * @brief 读取通知槽是否有待处理通知。
 * @param[in] task 任务句柄。
 * @param[in] index 槽号。
 * @return true 表示有。
 */
bool freertos_stub_notification_pending(TaskHandle_t task, UBaseType_t index)
{
    return task->pending[checked_slot(task, index)];
}

/**
 * @brief 读取通知槽成功被通知的次数。
 * @param[in] task 任务句柄。
 * @param[in] index 槽号。
 * @return 次数。
 */
uint32_t freertos_stub_notify_calls(TaskHandle_t task, UBaseType_t index)
{
    return task->calls[checked_slot(task, index)];
}

/**
 * @brief 向任务的通知槽发通知。
 * @param[in] task 目标任务。
 * @param[in] index 槽号。
 * @param[in] value 通知值（含义由 action 决定）。
 * @param[in] action 更新通知值的方式。
 * @retval pdPASS 已通知。
 * @retval pdFAIL 动作是 eSetValueWithoutOverwrite 而槽里已有待处理通知。
 */
BaseType_t xTaskNotifyIndexed(TaskHandle_t task, UBaseType_t index, uint32_t value, eNotifyAction action)
{
    UBaseType_t slot = checked_slot(task, index);

    if (notify_hook != NULL)
    {
        notify_hook(task, index, value, notify_hook_context);
    }
    if ((action == eSetValueWithoutOverwrite) && task->pending[slot])
    {
        return pdFAIL;
    }
    switch (action)
    {
    case eSetBits:
        task->value[slot] |= value;
        break;
    case eIncrement:
        task->value[slot]++;
        break;
    case eSetValueWithOverwrite:
    case eSetValueWithoutOverwrite:
        task->value[slot] = value;
        break;
    case eNoAction:
    default:
        break;
    }
    task->pending[slot] = true;
    task->calls[slot]++;
    return pdPASS;
}

/**
 * @brief 中断里向通知槽发通知；主机桩不请求让出。
 * @param[in] task 目标任务。
 * @param[in] index 槽号。
 * @param[in] value 通知值。
 * @param[in] action 更新方式。
 * @param[out] higher_priority_task_woken 可为 NULL，写入 pdFALSE。
 * @retval pdPASS 已通知。
 * @retval pdFAIL 同 xTaskNotifyIndexed。
 */
BaseType_t xTaskNotifyIndexedFromISR(TaskHandle_t task,
                                     UBaseType_t index,
                                     uint32_t value,
                                     eNotifyAction action,
                                     BaseType_t *higher_priority_task_woken)
{
    if (higher_priority_task_woken != NULL)
    {
        *higher_priority_task_woken = pdFALSE;
    }
    return xTaskNotifyIndexed(task, index, value, action);
}

/**
 * @brief 中断里给通知槽计数加一。
 * @param[in] task 目标任务。
 * @param[in] index 槽号。
 * @param[out] higher_priority_task_woken 可为 NULL，写入 pdFALSE。
 */
void vTaskNotifyGiveIndexedFromISR(TaskHandle_t task, UBaseType_t index, BaseType_t *higher_priority_task_woken)
{
    (void)xTaskNotifyIndexedFromISR(task, index, 0U, eIncrement, higher_priority_task_woken);
}

/**
 * @brief 清除通知槽的待处理状态，不改通知值。
 * @param[in] task 目标任务。
 * @param[in] index 槽号。
 * @retval pdTRUE 清除前有待处理通知。
 * @retval pdFALSE 清除前没有。
 */
BaseType_t xTaskNotifyStateClearIndexed(TaskHandle_t task, UBaseType_t index)
{
    UBaseType_t slot = checked_slot(task, index);
    bool was_pending = task->pending[slot];

    task->pending[slot] = false;
    return was_pending ? pdTRUE : pdFALSE;
}

/**
 * @brief 当前任务等待通知槽；没有通知时先调阻塞钩子，仍没有按超时失败。
 * @param[in] index 槽号。
 * @param[in] clear_on_entry 进入时（仅无待处理通知）清除的位。
 * @param[in] clear_on_exit 取到通知后清除的位。
 * @param[out] value 取到的通知值（清除之前），可为 NULL；失败时不写。
 * @param[in] ticks_to_wait 超时。
 * @retval pdTRUE 取到通知。
 * @retval pdFALSE 超时。
 */
BaseType_t xTaskNotifyWaitIndexed(UBaseType_t index,
                                  uint32_t clear_on_entry,
                                  uint32_t clear_on_exit,
                                  uint32_t *value,
                                  TickType_t ticks_to_wait)
{
    TaskHandle_t task = current_task;
    UBaseType_t slot = checked_slot(task, index);

    if (!task->pending[slot])
    {
        task->value[slot] &= ~clear_on_entry;
        if (ticks_to_wait != 0U)
        {
            FreertosStub_Wait wait = {FREERTOS_STUB_WAIT_NOTIFICATION, NULL, task, index, ticks_to_wait, NULL};

            run_block_hook(&wait);
        }
    }
    if (!task->pending[slot])
    {
        if (ticks_to_wait != 0U)
        {
            expire_wait(ticks_to_wait);
        }
        return pdFALSE;
    }
    if (value != NULL)
    {
        *value = task->value[slot];
    }
    task->value[slot] &= ~clear_on_exit;
    task->pending[slot] = false;
    return pdTRUE;
}

/**
 * @brief 当前任务把通知槽当计数信号量取；计数为 0 时先调阻塞钩子，仍为 0 按超时返回 0。
 * @param[in] index 槽号。
 * @param[in] clear_count_on_exit pdTRUE 取走后清零，否则减一。
 * @param[in] ticks_to_wait 超时。
 * @return 取之前的计数；超时为 0。
 */
uint32_t ulTaskNotifyTakeIndexed(UBaseType_t index, BaseType_t clear_count_on_exit, TickType_t ticks_to_wait)
{
    TaskHandle_t task = current_task;
    UBaseType_t slot = checked_slot(task, index);
    uint32_t count;

    if ((task->value[slot] == 0U) && (ticks_to_wait != 0U))
    {
        FreertosStub_Wait wait = {FREERTOS_STUB_WAIT_NOTIFICATION, NULL, task, index, ticks_to_wait, NULL};

        run_block_hook(&wait);
    }
    count = task->value[slot];
    if (count == 0U)
    {
        if (ticks_to_wait != 0U)
        {
            expire_wait(ticks_to_wait);
        }
        return 0U;
    }
    task->value[slot] = (clear_count_on_exit != pdFALSE) ? 0U : (count - 1U);
    task->pending[slot] = false;
    return count;
}

/**
 * @brief 取得事件组句柄背后的桩状态。
 * @param[in] group 事件组句柄。
 * @return 桩状态指针。
 */
static StubEventGroup *event_group_state(EventGroupHandle_t group)
{
    assert(group != NULL);
    return (StubEventGroup *)(void *)group;
}

/**
 * @brief 读取事件组的调用统计。
 * @param[in] group 事件组句柄。
 * @return 统计快照。
 */
FreertosStub_EventGroupStats freertos_stub_event_group_stats(EventGroupHandle_t group)
{
    return event_group_state(group)->stats;
}

/**
 * @brief 最近一次创建的事件组。
 * @return 句柄；reset 之后还没创建过则为 NULL。
 */
EventGroupHandle_t freertos_stub_last_created_event_group(void)
{
    return last_created_event_group;
}

/**
 * @brief 在调用者缓冲区里创建事件组，所有位为零。
 * @param[out] buffer 缓冲区。
 * @return 句柄，即缓冲区地址。
 */
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *buffer)
{
    assert(buffer != NULL);
    memset(buffer, 0, sizeof(*buffer));
    last_created_event_group = buffer;
    return buffer;
}

/**
 * @brief 置位。主机桩没有等待者列表：等待由阻塞钩子在“将要阻塞”时替别的任务置位来脚本化。
 * @param[in] group 事件组句柄。
 * @param[in] bits_to_set 要置的位。
 * @return 置位之后的位值。
 */
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits_to_set)
{
    StubEventGroup *state = event_group_state(group);

    state->stats.set_calls++;
    state->bits |= bits_to_set;
    consecutive_unsatisfied_infinite_waits = 0U;
    return state->bits;
}

/**
 * @brief 清位。
 * @param[in] group 事件组句柄。
 * @param[in] bits_to_clear 要清的位。
 * @return 清位之前的位值（与真实 FreeRTOS 一致）。
 */
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits_to_clear)
{
    StubEventGroup *state = event_group_state(group);
    const EventBits_t previous = state->bits;

    state->stats.clear_calls++;
    state->bits &= ~bits_to_clear;
    return previous;
}

/**
 * @brief 读当前位，不计入清位调用。
 * @param[in] group 事件组句柄。
 * @return 当前位值。
 */
EventBits_t xEventGroupGetBits(EventGroupHandle_t group)
{
    return event_group_state(group)->bits;
}

/**
 * @brief 判断位是否满足等待条件。
 * @param[in] current 当前位值。
 * @param[in] wanted 等待的位。
 * @param[in] wait_for_all pdTRUE 要求全部位，否则任一位。
 * @return true 条件满足。
 */
static bool event_bits_satisfied(EventBits_t current, EventBits_t wanted, BaseType_t wait_for_all)
{
    return (wait_for_all != pdFALSE) ? ((current & wanted) == wanted) : ((current & wanted) != 0U);
}

/**
 * @brief 等事件组的位；不满足且超时不为 0 时先调阻塞钩子，仍不满足按超时返回当前位值。
 * @param[in] group 事件组句柄。
 * @param[in] bits_to_wait_for 等待的位。
 * @param[in] clear_on_exit pdTRUE 时条件满足后清掉等待的位。
 * @param[in] wait_for_all_bits pdTRUE 要求全部位。
 * @param[in] ticks_to_wait 超时，0 表示不等，portMAX_DELAY 表示无限。
 * @return 返回时的位值（清位之前）；超时时条件不满足。
 */
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group,
                                EventBits_t bits_to_wait_for,
                                BaseType_t clear_on_exit,
                                BaseType_t wait_for_all_bits,
                                TickType_t ticks_to_wait)
{
    StubEventGroup *state = event_group_state(group);
    EventBits_t result;

    state->stats.wait_calls++;
    if (!event_bits_satisfied(state->bits, bits_to_wait_for, wait_for_all_bits) && (ticks_to_wait != 0U))
    {
        FreertosStub_Wait wait = {FREERTOS_STUB_WAIT_EVENT_GROUP, NULL, NULL, 0U, ticks_to_wait, group};

        state->stats.blocked_waits++;
        run_block_hook(&wait);
    }

    result = state->bits;
    if (event_bits_satisfied(result, bits_to_wait_for, wait_for_all_bits))
    {
        consecutive_unsatisfied_infinite_waits = 0U;
        if (clear_on_exit != pdFALSE)
        {
            state->bits &= ~bits_to_wait_for;
        }
    }
    else if (ticks_to_wait != 0U)
    {
        expire_wait(ticks_to_wait);
    }
    return result;
}
