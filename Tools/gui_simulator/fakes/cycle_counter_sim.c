/**
  ******************************************************************************
  * @file    cycle_counter_sim.c
  * @brief   Cortex-M7 周期计数器替身：用 SDL 高精度计数器代替 DWT。
  *
  * @details
  *          只供 Service/gui 字体子模块测量解码耗时写入启动日志；日志不参与帧哈希，
  *          PC 上的耗时不代表板上表现。
  ******************************************************************************
  */

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"

#include <SDL.h>

/**
 * @brief 同生产头：启动计数器；SDL 计数器始终运行。
 * @return 始终为 true。
 */
bool CortexM7CycleCounter_Start(void)
{
    return true;
}

/**
 * @brief 同生产头：读当前计数（低 32 位，可回绕）。
 * @return SDL 高精度计数器的低 32 位。
 */
uint32_t CortexM7CycleCounter_Read(void)
{
    return (uint32_t)SDL_GetPerformanceCounter();
}

/**
 * @brief 同生产头：计数频率。
 * @return SDL 高精度计数器频率；超出 32 位时返回 0（调用者按不可用处理）。
 */
uint32_t CortexM7CycleCounter_GetFrequencyHz(void)
{
    const Uint64 frequency = SDL_GetPerformanceFrequency();

    return (frequency > 0xFFFFFFFFULL) ? 0U : (uint32_t)frequency;
}
