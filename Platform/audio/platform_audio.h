/**
  ******************************************************************************
  * @file    platform_audio.h
  * @brief   本板音频 I2S 循环 DMA 输出的 Platform Interface。
  *
  * @details
  *          播放缓冲区是由调用者持有的环形缓冲区，分为两个等长半区；DMA 循环读取，
  *          每读完一个半区就累加计数并经单槽回调唤醒调用者。回调运行在 ISR 上下文，
  *          只能执行 FromISR 安全的轻量唤醒；任务经 Platform_Audio_GetStatus()
  *          读取已释放半区数、欠载数与故障。XSMT 静音只由本 Module 编排。
  ******************************************************************************
  */

#ifndef PLATFORM_AUDIO_H
#define PLATFORM_AUDIO_H

#include <stdint.h>

#include "Platform/platform.h"

/** @brief 输出故障。STOP_FAILED 锁存，重启前不解除。 */
typedef enum
{
    PLATFORM_AUDIO_FAULT_NONE = 0U,
    PLATFORM_AUDIO_FAULT_TRANSFER_ERROR,
    PLATFORM_AUDIO_FAULT_STOP_FAILED
} Platform_Audio_FaultTypeDef;

/** @brief 输出完成事实快照。 */
typedef struct
{
    uint32_t ReleasedHalves;          /**< 本次 Start 以来 DMA 读完的半区数；第 n 次释放的是半区 (n-1) 的奇偶，即奇数次为半区 0，偶数次为半区 1。 */
    uint32_t Underruns;               /**< 释放半区时另一半区未提交的次数。 */
    Platform_Audio_FaultTypeDef Fault;
} Platform_Audio_StatusTypeDef;

/** @brief 单槽事件回调。在 ISR 中调用，只唤醒。 */
typedef void (*Platform_Audio_EventCallback_t)(void *context);

Platform_StatusTypeDef Platform_Audio_Init(void);
Platform_StatusTypeDef Platform_Audio_SetEventCallback(Platform_Audio_EventCallback_t callback, void *context);
Platform_StatusTypeDef Platform_Audio_ClearEventCallback(void);
Platform_StatusTypeDef Platform_Audio_Start(uint32_t sample_rate, int16_t *ring, uint32_t frames_per_half);
Platform_StatusTypeDef Platform_Audio_SubmitHalf(uint32_t half);
Platform_StatusTypeDef Platform_Audio_GetStatus(Platform_Audio_StatusTypeDef *status);
Platform_StatusTypeDef Platform_Audio_Stop(void);

#endif /* PLATFORM_AUDIO_H */
