/**
  ******************************************************************************
  * @file    spike_audio_read.c
  * @brief   音频流读块耗时统计的实现（spike #191）。
  ******************************************************************************
  */

#include "APP/tasks/foreground/spike_audio_read.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

static ForegroundStatTypeDef spike_audio_read_stat;

/**
 * @brief 记一次整块音频读取的持锁耗时。
 * @param[in] hold_us 拿到锁之后到 ReadFile 返回的微秒数。
 * @note  Storage Task 调用，每块一次（不是每帧）；临界区只有几条指令。
 */
void spike_audio_read_note(uint32_t hold_us)
{
    taskENTER_CRITICAL();
    foreground_stat_add(&spike_audio_read_stat, hold_us);
    taskEXIT_CRITICAL();
}

/**
 * @brief 拷出自上次清零以来的统计。
 * @param[out] stat 接收统计。
 */
void spike_audio_read_get(ForegroundStatTypeDef *stat)
{
    taskENTER_CRITICAL();
    *stat = spike_audio_read_stat;
    taskEXIT_CRITICAL();
}

/**
 * @brief 清零统计（阶段开始时由前台任务调用）。
 */
void spike_audio_read_reset(void)
{
    taskENTER_CRITICAL();
    foreground_stat_reset(&spike_audio_read_stat);
    taskEXIT_CRITICAL();
}
