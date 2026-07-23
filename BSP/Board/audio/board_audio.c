#include "BSP/Board/board.h"
#include "BSP/Devices/audio/audio.h"
#include "BSP/Devices/audio/port/audio_port.h"
#include "System/Log/log.h"

static Audio_HandleTypeDef haudio;

/**
  * @brief  通过本板 Audio Device 发送一段 PCM 数据。
  * @param  data PCM 数据缓冲区。
  * @param  size 待发送的 16 位数据数量。
  * @retval BOARD_OK 发送成功。
  * @retval BOARD_AUDIO_ERROR 参数、状态或底层 I2S 发送失败。
  */
Board_StatusTypeDef Board_Audio_Transmit(const uint16_t *data, uint16_t size)
{
    if (Audio_Transmit(&haudio, data, size) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "transmit failed: error=%d, bus=%d",
                         (int)haudio.ErrorCode,
                         (int)haudio.LastBusStatus);

        return BOARD_AUDIO_ERROR;
    }

    return BOARD_OK;
}

/**
  * @brief  设置本板音频输出的静音状态。
  * @param  mute true 表示静音，false 表示解除静音。
  * @retval BOARD_OK 静音状态设置成功。
  * @retval BOARD_AUDIO_ERROR Audio Device 或底层静音控制失败。
  */
Board_StatusTypeDef Board_Audio_SetMute(bool mute)
{
    if (Audio_Mute(&haudio, mute) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "mute control failed: error=%d, bus=%d",
                         (int)haudio.ErrorCode,
                         (int)haudio.LastBusStatus);

        return BOARD_AUDIO_ERROR;
    }

    return BOARD_OK;
}

/**
  * @brief  绑定本板音频 Port 并初始化唯一的 Audio Device 实例。
  * @retval BOARD_OK 初始化完成，Audio Device 进入 READY 状态。
  * @retval BOARD_AUDIO_ERROR Port 绑定或 Audio Device 初始化失败。
  */
Board_StatusTypeDef Board_Audio_Init(void)
{
    if (Audio_Port_Bind(&haudio) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "AUDIO", "Audio port bind failed");
        return BOARD_AUDIO_ERROR;
    }
    Audio_StatusTypeDef audio_status = Audio_Init(&haudio);
    if (audio_status != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "initialization failed: error=%d, bus=%d",
                         (int)haudio.ErrorCode,
                         (int)haudio.LastBusStatus);
        return BOARD_AUDIO_ERROR;
    }

    return BOARD_OK;
}
