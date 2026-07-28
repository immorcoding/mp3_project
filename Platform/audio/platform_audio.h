/**
  ******************************************************************************
  * @file    platform_audio.h
  * @brief   本板音频初始化、同步发送和静音控制的 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_AUDIO_H
#define PLATFORM_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#include "Platform/platform.h"

Platform_StatusTypeDef Platform_Audio_SetMute(bool mute);
Platform_StatusTypeDef Platform_Audio_Transmit(const uint16_t *data, uint16_t size);

Platform_StatusTypeDef Platform_Audio_Init(void);

#endif // PLATFORM_AUDIO_H
