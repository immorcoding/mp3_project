/**
 * @file lock_env_cycle_counter.c
 * @brief Cortex-M7 周期计数器的主机替身；脚本化的时间，行为见 lock_env_stub.h。
 */

#include "lock_env_stub.h"

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"

#define LOCK_ENV_FREQUENCY_HZ 480000000U

static uint32_t fake_cycles;
static bool counter_unavailable;

/**
 * @brief 周期计数器归零，频率恢复为 480 MHz。
 */
void lock_env_counter_reset(void)
{
    fake_cycles = 0U;
    counter_unavailable = false;
}

/**
 * @brief 让假计数器前进。
 * @param[in] microseconds 微秒数。
 */
void lock_env_advance_us(uint32_t microseconds)
{
    fake_cycles += microseconds * (LOCK_ENV_FREQUENCY_HZ / 1000000U);
}

/**
 * @brief 设定计数器是否“未启动”（频率为 0）。
 * @param[in] unavailable true 为未启动。
 */
void lock_env_set_counter_unavailable(bool unavailable)
{
    counter_unavailable = unavailable;
}

/**
 * @brief 周期计数器替身：启动恒成功。
 * @retval true 恒成功。
 */
bool CortexM7CycleCounter_Start(void)
{
    return true;
}

/**
 * @brief 周期计数器替身：读脚本化的当前周期数。
 * @return 周期数。
 */
uint32_t CortexM7CycleCounter_Read(void)
{
    return fake_cycles;
}

/**
 * @brief 周期计数器替身：固定 480 MHz，或按脚本报告 0。
 * @return 频率（Hz）。
 */
uint32_t CortexM7CycleCounter_GetFrequencyHz(void)
{
    return counter_unavailable ? 0U : LOCK_ENV_FREQUENCY_HZ;
}
