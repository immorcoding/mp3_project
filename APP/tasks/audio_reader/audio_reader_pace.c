/**
 ******************************************************************************
 * @file    audio_reader_pace.c
 * @brief   音频读取任务“决定等多久”的纯函数（见 audio_reader_pace.h）。
 ******************************************************************************
 */

#include "APP/tasks/audio_reader/audio_reader_pace.h"

/**
 * @brief  决定本圈末尾在唯一的等待点上等多久。
 * @param[in] input 当前事实；不得为空。
 * @return 要等的 tick 数：0 不等，`AUDIO_READER_WAIT_FOREVER` 一直等，其余为有限等待（让路 1 tick 或 SERVICE_BUSY 重试时刻）。
 * @note   让路用等待点而不是 vTaskDelay：让路期间新请求、块归还、卷事件都能立刻叫醒任务。水位按就绪队列算，
 *         不含播放任务手里那块（由调用者传入就绪队列的块数）。
 */
uint32_t audio_reader_wait_ticks(const AudioReader_PaceInputTypeDef *input)
{
    if (input->RequestPending)
    {
        return 0U;
    }
    if (input->RetryActive)
    {
        return input->RetryRemainingTicks;
    }
    if (!input->StreamOpen || (input->FreeBlocks == 0U))
    {
        return AUDIO_READER_WAIT_FOREVER;
    }
    if (input->ReadyBlocks < AUDIO_READER_WATERMARK_BLOCKS)
    {
        return 0U;
    }
    return AUDIO_READER_YIELD_TICKS;
}
