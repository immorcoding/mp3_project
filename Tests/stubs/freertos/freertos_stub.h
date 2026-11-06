/**
 * @file freertos_stub.h
 * @brief 主机共用 FreeRTOS 桩的脚本化与观察接口。
 *
 * 主机测试是单线程的：桩不切换任务，也不会真的阻塞。拿不到锁、信号量或通知时，
 * 先调用阻塞钩子（让测试替“别的任务或中断”做点什么），再检查一次，仍然拿不到就
 * 让 tick 前进超时长度并返回失败。桩里的通过不证明真实并发正确。
 */
#ifndef HOST_STUB_FREERTOS_STUB_H
#define HOST_STUB_FREERTOS_STUB_H

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/event_groups.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief 阻塞钩子被调用时，正在等待的是什么。 */
typedef enum
{
    FREERTOS_STUB_WAIT_SEMAPHORE = 1, /**< 等待互斥锁或二值信号量。 */
    FREERTOS_STUB_WAIT_NOTIFICATION,  /**< 等待任务通知。 */
    FREERTOS_STUB_WAIT_EVENT_GROUP    /**< 等待事件组的位。 */
} FreertosStub_WaitKind;

/** @brief 一次将要阻塞的等待。 */
typedef struct
{
    FreertosStub_WaitKind kind;  /**< 等待的种类。 */
    SemaphoreHandle_t semaphore; /**< 等待信号量时的句柄，否则为 NULL。 */
    TaskHandle_t task;           /**< 等待通知时的当前任务，否则为 NULL。 */
    UBaseType_t index;           /**< 等待通知时的槽号。 */
    TickType_t ticks;            /**< 调用者给的超时，可能是 portMAX_DELAY。 */
    EventGroupHandle_t event_group; /**< 等待事件组时的句柄，否则为 NULL（省略的初始化项即为 NULL）。 */
} FreertosStub_Wait;

/**
 * @brief 阻塞钩子：等待将要发生时调用一次，替“别的任务或中断”做事（如释放锁、给通知）。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context 安装钩子时给的上下文。
 */
typedef void (*FreertosStub_BlockHook)(const FreertosStub_Wait *wait, void *context);

/**
 * @brief 通知钩子：每次 xTaskNotifyIndexed（含 FromISR 版本）在通知落进目标槽之前调用一次。
 *        用来看“这次通知发出时被测模块的状态”，例如证明卷事件是先更新状态再叫醒订阅者。
 * @param[in] task 目标任务。
 * @param[in] index 槽号。
 * @param[in] value 通知值（含义由动作决定）。
 * @param[in,out] context 安装钩子时给的上下文。
 */
typedef void (*FreertosStub_NotifyHook)(TaskHandle_t task, UBaseType_t index, uint32_t value, void *context);

/** @brief 某个信号量（互斥锁或二值信号量）的调用统计。 */
typedef struct
{
    uint32_t take_calls;         /**< 拿取调用总数（含失败）。 */
    uint32_t take_successes;     /**< 拿取成功次数。 */
    uint32_t waits;              /**< 拿取时对象不可用且超时不为 0 的次数。 */
    uint32_t give_calls;         /**< 释放调用总数（含失败，含中断里的释放）。 */
    uint32_t give_from_isr_calls; /**< 其中经 xSemaphoreGiveFromISR 的调用数（含失败）。 */
    uint32_t give_successes;     /**< 释放成功次数。 */
    TickType_t last_timeout_ticks; /**< 最近一次拿取传入的超时。 */
} FreertosStub_SemaphoreStats;

/** @brief 某个事件组的调用统计。 */
typedef struct
{
    uint32_t wait_calls;    /**< 等待调用总数（含立刻满足的）。 */
    uint32_t blocked_waits; /**< 等待时位不满足且超时不为 0 的次数（即将进入阻塞钩子）。 */
    uint32_t set_calls;     /**< 置位调用次数。 */
    uint32_t clear_calls;   /**< 清位调用次数。 */
} FreertosStub_EventGroupStats;

/**
 * @brief 读取某个事件组的调用统计。
 * @param[in] group 事件组句柄。
 * @return 统计快照。
 */
FreertosStub_EventGroupStats freertos_stub_event_group_stats(EventGroupHandle_t group);

/**
 * @brief 最近一次创建的事件组；被测模块把事件组藏在内部时，测试在调用模块之后用它取得句柄。
 * @return 句柄；reset 之后还没创建过则为 NULL。
 * @note  reset 只清这个记录，不清被测模块静态存储里的事件组：模块的事件组是第一次使用时才创建的，
 *        所以测试应当在第一次调用被测模块之前 reset，之后只比较统计的增量。
 */
EventGroupHandle_t freertos_stub_last_created_event_group(void);

/**
 * @brief 丢弃所有对象与脚本，tick 归零、步长归零。
 */
void freertos_stub_reset(void);

/**
 * @brief 设定当前 tick。
 * @param[in] tick 新的 tick 值。
 */
void freertos_stub_set_tick(TickType_t tick);

/**
 * @brief 设定每次 xTaskGetTickCount 调用前自动前进的 tick 数。
 * @param[in] step 步长；0 表示 tick 只随超时等待前进。
 */
void freertos_stub_set_tick_step(TickType_t step);

/**
 * @brief 安装阻塞钩子；传 NULL 取消。钩子返回后桩只重新检查一次，仍不可用就按超时失败。
 * @param[in] hook 钩子函数。
 * @param[in,out] context 传给钩子的上下文。
 */
void freertos_stub_set_block_hook(FreertosStub_BlockHook hook, void *context);

/**
 * @brief 安装通知钩子；传 NULL 取消，reset 也会取消。钩子在通知落进槽之前调用，失败的 WithoutOverwrite 通知也会先经过它。
 * @param[in] hook 钩子函数。
 * @param[in,out] context 传给钩子的上下文。
 */
void freertos_stub_set_notify_hook(FreertosStub_NotifyHook hook, void *context);

/**
 * @brief 无限等待（portMAX_DELAY）后仍无人放行的次数；真实环境里这些调用会一直阻塞。
 * @return 累计次数，reset 后清零。
 */
uint32_t freertos_stub_unsatisfied_infinite_waits(void);

/**
 * @brief 让之后每次 xSemaphoreGiveFromISR 报告的 higher_priority_task_woken 为给定值，
 *        用来核对被测回调把它交给了 portYIELD_FROM_ISR；reset 后恢复 pdFALSE。
 * @param[in] woken pdTRUE 或 pdFALSE。
 */
void freertos_stub_set_give_from_isr_woken(BaseType_t woken);

/**
 * @brief portYIELD_FROM_ISR 以非零标志被调用的次数（桩不真的让出）。
 * @return 累计次数，reset 后清零。
 */
uint32_t freertos_stub_isr_yield_requests(void);

/**
 * @brief 读取某个信号量的调用统计。
 * @param[in] semaphore 信号量句柄。
 * @return 统计快照。
 */
FreertosStub_SemaphoreStats freertos_stub_semaphore_stats(SemaphoreHandle_t semaphore);

/**
 * @brief 最近一次创建（动态或静态）的信号量；被测模块把锁藏在内部、不交出句柄时，
 *        测试在调用模块的初始化后用它取得句柄，再配合 fail_next_takes 与统计。
 * @return 句柄；reset 之后还没创建过则为 NULL。
 */
SemaphoreHandle_t freertos_stub_last_created_semaphore(void);

/**
 * @brief 自 reset 以来创建（动态或静态）的信号量个数；被测模块有多把内部锁时，测试按创建顺序枚举。
 *        列表最多记 32 个，超出的不记。
 * @return 个数，reset 后清零。
 */
uint32_t freertos_stub_created_semaphore_count(void);

/**
 * @brief 按创建顺序取第 index 个创建的信号量。
 * @param[in] index 从 0 起的序号。
 * @return 句柄；序号超出已创建个数时为 NULL。
 * @note  reset 只清列表，不清被测模块自己静态存储里的信号量：模块的锁是第一次使用时才创建的，
 *        所以测试应当在第一次调用被测模块之前 reset，之后只比较统计的增量，不再 reset。
 */
SemaphoreHandle_t freertos_stub_created_semaphore(uint32_t index);

/**
 * @brief 让某个信号量接下来的 N 次拿取失败（即使它空闲），用来模拟别的任务占着锁。
 * @param[in] semaphore 信号量句柄。
 * @param[in] count 失败次数；失败的拿取不改变持有状态，仍按超时前进 tick。
 */
void freertos_stub_semaphore_fail_next_takes(SemaphoreHandle_t semaphore, uint32_t count);

/**
 * @brief 新建一个假任务，用来当通知的目标；它不会运行。
 * @return 任务句柄；任务池用尽返回 NULL。reset 之后句柄失效。
 */
TaskHandle_t freertos_stub_task_create(void);

/**
 * @brief 指定“当前任务”，影响 xTaskGetCurrentTaskHandle 以及无句柄的等待类调用。
 * @param[in] task 任务句柄；reset 后默认是桩自带的主任务。
 */
void freertos_stub_set_current_task(TaskHandle_t task);

/**
 * @brief 读取某个通知槽的值。
 * @param[in] task 任务句柄。
 * @param[in] index 槽号。
 * @return 当前通知值。
 */
uint32_t freertos_stub_notification_value(TaskHandle_t task, UBaseType_t index);

/**
 * @brief 某个通知槽是否有尚未被等待取走的通知。
 * @param[in] task 任务句柄。
 * @param[in] index 槽号。
 * @return true 表示有待处理通知。
 */
bool freertos_stub_notification_pending(TaskHandle_t task, UBaseType_t index);

/**
 * @brief 某个通知槽被通知的次数（失败的 WithoutOverwrite 不计）。
 * @param[in] task 任务句柄。
 * @param[in] index 槽号。
 * @return 成功通知次数，reset 后清零。
 */
uint32_t freertos_stub_notify_calls(TaskHandle_t task, UBaseType_t index);

#endif
