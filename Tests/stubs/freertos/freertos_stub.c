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
static TickType_t current_tick;
static TickType_t tick_step;
static FreertosStub_BlockHook block_hook;
static void *block_hook_context;
static uint32_t infinite_waits_unsatisfied;

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
    current_tick = 0U;
    tick_step = 0U;
    block_hook = NULL;
    block_hook_context = NULL;
    infinite_waits_unsatisfied = 0U;
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
        FreertosStub_Wait wait = {FREERTOS_STUB_WAIT_SEMAPHORE, semaphore, NULL, 0U, ticks_to_wait};

        state->stats.waits++;
        run_block_hook(&wait);
    }

    if (!scripted_failure && state->count > 0U)
    {
        state->count--;
        state->stats.take_successes++;
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
    return pdTRUE;
}

/**
 * @brief 中断里释放信号量；主机桩没有调度，不请求让出。
 * @param[in] semaphore 信号量句柄。
 * @param[out] higher_priority_task_woken 可为 NULL，写入 pdFALSE。
 * @retval pdTRUE 释放成功。
 * @retval pdFALSE 信号量本来就可用。
 */
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *higher_priority_task_woken)
{
    if (higher_priority_task_woken != NULL)
    {
        *higher_priority_task_woken = pdFALSE;
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
            FreertosStub_Wait wait = {FREERTOS_STUB_WAIT_NOTIFICATION, NULL, task, index, ticks_to_wait};

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
        FreertosStub_Wait wait = {FREERTOS_STUB_WAIT_NOTIFICATION, NULL, task, index, ticks_to_wait};

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
