/**
  ******************************************************************************
  * @file    sim_clock.h
  * @brief   模拟器时钟：实时（SDL 毫秒）或确定性虚拟时钟。
  *
  * @details
  *          确定性模式下每圈主循环固定推进 SIM_CLOCK_STEP_MS，与墙钟无关；
  *          同一组脚本输入总是得到逐像素相同的帧，用于基线比对。
  ******************************************************************************
  */

#ifndef SIM_CLOCK_H
#define SIM_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 确定性模式下每圈主循环推进的毫秒数。 */
#define SIM_CLOCK_STEP_MS  5U

void sim_clock_set_deterministic(bool deterministic);
bool sim_clock_is_deterministic(void);

/** @brief 当前毫秒数；FreeRTOS Tick 替身、脚本与演示分区共用。 */
uint32_t sim_clock_now(void);

/** @brief 主循环每圈调用一次；确定性模式下推进虚拟时钟。 */
void sim_clock_tick(void);

#endif /* SIM_CLOCK_H */
