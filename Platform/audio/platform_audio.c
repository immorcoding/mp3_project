#include "Platform/platform.h"
#include "Components/audio/audio.h"
#include "Adapters/audio_i2s/audio_i2s_stm32_hal_adapter.h"
#include "Components/log/log.h"
#include "i2s.h"
#include "main.h"

static Audio_HandleTypeDef haudio;

static AudioI2S_STM32HALAdapterTypeDef hplatform_audio_adapter = {
    .I2SHandle = &hi2s2,
    .MutePort = PCM_XSMT_GPIO_Port,
    .MutePin = PCM_XSMT_Pin
};

/**
  * @brief  通过本板 Audio Device 发送一段 PCM 数据。
  * @param  data PCM 数据缓冲区。
  * @param  size 待发送的 16 位数据数量。
  * @retval PLATFORM_OK 发送成功。
  * @retval PLATFORM_AUDIO_ERROR 参数、状态或底层 I2S 发送失败。
  */
Platform_StatusTypeDef Platform_Audio_Transmit(const uint16_t *data, uint16_t size)
{
    if (Audio_Transmit(&haudio, data, size) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "transmit failed: error=%d, bus=%d",
                         (int)haudio.ErrorCode,
                         (int)haudio.LastBusStatus);

        return PLATFORM_AUDIO_ERROR;
    }

    return PLATFORM_OK;
}

/**
  * @brief  设置本板音频输出的静音状态。
  * @param  mute true 表示静音，false 表示解除静音。
  * @retval PLATFORM_OK 静音状态设置成功。
  * @retval PLATFORM_AUDIO_ERROR Audio Device 或底层静音控制失败。
  */
Platform_StatusTypeDef Platform_Audio_SetMute(bool mute)
{
    if (Audio_Mute(&haudio, mute) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "mute control failed: error=%d, bus=%d",
                         (int)haudio.ErrorCode,
                         (int)haudio.LastBusStatus);

        return PLATFORM_AUDIO_ERROR;
    }

    return PLATFORM_OK;
}

/**
  * @brief  绑定本板音频 Port 并初始化唯一的 Audio Device 实例。
  * @retval PLATFORM_OK 初始化完成，Audio Device 进入 READY 状态。
  * @retval PLATFORM_AUDIO_ERROR Port 绑定或 Audio Device 初始化失败。
  */
Platform_StatusTypeDef Platform_Audio_Init(void)
{
    if (AudioI2S_STM32HALAdapter_Bind(
            &haudio,
            &hplatform_audio_adapter) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "AUDIO", "Audio port bind failed");
        return PLATFORM_AUDIO_ERROR;
    }
    Audio_StatusTypeDef audio_status = Audio_Init(&haudio);
    if (audio_status != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "initialization failed: error=%d, bus=%d",
                         (int)haudio.ErrorCode,
                         (int)haudio.LastBusStatus);
        return PLATFORM_AUDIO_ERROR;
    }

    return PLATFORM_OK;
}
