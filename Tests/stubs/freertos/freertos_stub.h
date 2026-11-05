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
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief 阻塞钩子被调用时，正在等待的是什么。 */
typedef enum
{
    FREERTOS_STUB_WAIT_SEMAPHORE = 1, /**< 等待互斥锁或二值信号量。 */
    FREERTOS_STUB_WAIT_NOTIFICATION   /**< 等待任务通知。 */
} FreertosStub_WaitKind;

/** @brief 一次将要阻塞的等待。 */
typedef struct
{
    FreertosStub_WaitKind kind;  /**< 等待的种类。 */
    SemaphoreHandle_t semaphore; /**< 等待信号量时的句柄，否则为 NULL。 */
    TaskHandle_t task;           /**< 等待通知时的当前任务，否则为 NULL。 */
    UBaseType_t index;           /**< 等待通知时的槽号。 */
    TickType_t ticks;            /**< 调用者给的超时，可能是 portMAX_DELAY。 */
} FreertosStub_Wait;

/**
 * @brief 阻塞钩子：等待将要发生时调用一次，替“别的任务或中断”做事（如释放锁、给通知）。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context 安装钩子时给的上下文。
 */
typedef void (*FreertosStub_BlockHook)(const FreertosStub_Wait *wait, void *context);

/** @brief 某个信号量（互斥锁或二值信号量）的调用统计。 */
typedef struct
{
    uint32_t take_calls;         /**< 拿取调用总数（含失败）。 */
    uint32_t take_successes;     /**< 拿取成功次数。 */
    uint32_t waits;              /**< 拿取时对象不可用且超时不为 0 的次数。 */
    uint32_t give_calls;         /**< 释放调用总数（含失败）。 */
    uint32_t give_successes;     /**< 释放成功次数。 */
    TickType_t last_timeout_ticks; /**< 最近一次拿取传入的超时。 */
} FreertosStub_SemaphoreStats;

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
 * @brief 无限等待（portMAX_DELAY）后仍无人放行的次数；真实环境里这些调用会一直阻塞。
 * @return 累计次数，reset 后清零。
 */
uint32_t freertos_stub_unsatisfied_infinite_waits(void);

/**
 * @brief 读取某个信号量的调用统计。
 * @param[in] semaphore 信号量句柄。
 * @return 统计快照。
 */
FreertosStub_SemaphoreStats freertos_stub_semaphore_stats(SemaphoreHandle_t semaphore);

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
