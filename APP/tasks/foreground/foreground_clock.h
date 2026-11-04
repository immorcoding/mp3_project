/**
  ******************************************************************************
  * @file    foreground_clock.h
  * @brief   前台任务量一次操作墙钟耗时的小工具：DWT 周期数加 tick 兜底（spike #199）。
  *
  * @details
  *          和 Filesystem 锁统计同一个口径：区间不足 4 s 时按 DWT 周期数取微秒，超过 4 s 时 DWT
  *          （32 位，480 MHz 下约 8.9 s 回绕）不再可信，改按 tick 取整到毫秒。墙钟包含等锁时间，
  *          持锁时长要另用 `Service_Filesystem_SpikeLastHoldUs()` 读。
  ******************************************************************************
  */

#ifndef FOREGROUND_CLOCK_H
#define FOREGROUND_CLOCK_H

#include <stdint.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 超过这个 tick 数的区间不再信 32 位 DWT。 */
#define FOREGROUND_CLOCK_DWT_LIMIT_TICKS 4000U

/** @brief 起点时间戳。 */
typedef struct
{
    TickType_t Tick;
    uint32_t Cycles;
} ForegroundClockStampTypeDef;

/**
 * @brief 取当前时间戳。
 * @return 周期数与 tick 的组合。
 */
static inline ForegroundClockStampTypeDef foreground_clock_now(void)
{
    ForegroundClockStampTypeDef stamp;

    stamp.Tick = xTaskGetTickCount();
    stamp.Cycles = CortexM7CycleCounter_Read();
    return stamp;
}

/**
 * @brief 从起点到现在的微秒数。
 * @param[in] start 起点时间戳。
 * @return 微秒；区间不足 4 s 且 DWT 可用时按周期数，否则按 tick 取整到毫秒。
 */
static inline uint32_t foreground_clock_elapsed_us(const ForegroundClockStampTypeDef *start)
{
    const TickType_t tick_delta = xTaskGetTickCount() - start->Tick;
    const uint32_t cycles_per_us = CortexM7CycleCounter_GetFrequencyHz() / 1000000U;

    if ((cycles_per_us == 0U) || (tick_delta >= FOREGROUND_CLOCK_DWT_LIMIT_TICKS))
    {
        return (uint32_t)tick_delta * (1000000U / (uint32_t)configTICK_RATE_HZ);
    }
    return (CortexM7CycleCounter_Read() - start->Cycles) / cycles_per_us;
}

#endif /* FOREGROUND_CLOCK_H */
