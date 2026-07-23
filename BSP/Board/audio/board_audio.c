#include "BSP/Board/board.h"
#include "BSP/Devices/audio/audio.h"
#include "BSP/Devices/audio/port/audio_port.h"
#include "System/Log/log.h"

static Audio_HandleTypeDef haudio;

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
