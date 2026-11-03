/**
  ******************************************************************************
  * @file    spike_time.h
  * @brief   可丢弃分支 spike/app-tasks（#191）的耗时测量：DWT 周期数加 tick 兜底。
  *
  * @details
  *          DWT 周期计数器是 32 位，480 MHz 下约 8.9 s 回绕，长操作（Flash 写盘、等锁、拔卡后的在途读取）
  *          会超过它。所以每个起点同时记周期数和 tick：区间不足 4 s 且 DWT 可用时按周期数（微秒精度），
  *          否则按 tick 取整到毫秒。只在任务上下文使用；计数器由 `app_init()` 统一启动，这里只读。
  ******************************************************************************
  */

#ifndef SPIKE_TIME_H
#define SPIKE_TIME_H

#include <stdint.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 超过这个 tick 数的区间不再信 32 位 DWT（4 s）。 */
#define SPIKE_TIME_DWT_LIMIT_TICKS 4000U

/** @brief 起点时间戳：周期数加 tick。 */
typedef struct
{
    uint32_t Cycles; /**< DWT 周期计数器读数。 */
    TickType_t Tick; /**< FreeRTOS tick 计数。 */
} spike_stamp_t;

/**
 * @brief 取当前时间戳。
 * @return 周期数与 tick 的组合。
 */
static inline spike_stamp_t spike_stamp_now(void)
{
    spike_stamp_t stamp;

    stamp.Tick = xTaskGetTickCount();
    stamp.Cycles = CortexM7CycleCounter_Read();
    return stamp;
}

/**
 * @brief 计算从 start 到现在经过的微秒数。
 * @param[in] start 起点时间戳。
 * @return 微秒；区间不足 4 s 且 DWT 可用时按周期数，否则按 tick 取整到毫秒。
 */
static inline uint32_t spike_stamp_elapsed_us(const spike_stamp_t *start)
{
    const TickType_t tick_delta = xTaskGetTickCount() - start->Tick;
    const uint32_t cycles_per_us = CortexM7CycleCounter_GetFrequencyHz() / 1000000U;

    if ((cycles_per_us == 0U) || (tick_delta >= SPIKE_TIME_DWT_LIMIT_TICKS))
    {
        return (uint32_t)tick_delta * (1000000U / (uint32_t)configTICK_RATE_HZ);
    }

    return (CortexM7CycleCounter_Read() - start->Cycles) / cycles_per_us;
}

#endif /* SPIKE_TIME_H */
