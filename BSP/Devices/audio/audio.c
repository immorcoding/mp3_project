#include "audio.h"
#include <stdbool.h>

static void audio_clear_error(Audio_HandleTypeDef *haudio)
{
    haudio->ErrorCode = AUDIO_ERROR_NONE;
    haudio->LastBusStatus = AUDIO_BUS_OK;
}

static Audio_StatusTypeDef audio_fail(Audio_HandleTypeDef *haudio,
                                      Audio_ErrorTypeDef error,
                                      Audio_BusStatusTypeDef bus_status,
                                      Audio_StateTypeDef next_state)
{
    haudio->ErrorCode = error;
    haudio->LastBusStatus = bus_status;
    haudio->State = next_state;
    return AUDIO_ERROR;
}

Audio_StatusTypeDef Audio_Transmit(Audio_HandleTypeDef *haudio,
                                    const uint16_t *data,
                                    uint16_t size)
{
    if (haudio == NULL)
    {
        return AUDIO_ERROR;
    }

    if ((data == NULL) || (size == 0U))
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_INVALID_PARAM,
                          AUDIO_BUS_OK,
                          haudio->State);
    }

    if ((haudio->BusOps == NULL) ||
        (haudio->BusOps->Transmit == NULL) ||
        (haudio->BusContext == NULL))
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_PORT_NOT_BOUND,
                          AUDIO_BUS_OK,
                          AUDIO_STATE_ERROR);
    }

    if (haudio->State != AUDIO_STATE_READY)
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_NOT_READY,
                          AUDIO_BUS_OK,
                          haudio->State);
    }

    audio_clear_error(haudio);
    haudio->State = AUDIO_STATE_BUSY;

    Audio_BusStatusTypeDef result =
        haudio->BusOps->Transmit(haudio->BusContext,
                                 data,
                                 size);

    if (result != AUDIO_BUS_OK)
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_BUS_TRANSMIT,
                          result,
                          (result == AUDIO_BUS_BUSY)
                              ? AUDIO_STATE_READY
                              : AUDIO_STATE_ERROR);
    }

    haudio->State = AUDIO_STATE_READY;
    return AUDIO_OK;
}

Audio_StatusTypeDef Audio_Mute(Audio_HandleTypeDef *haudio, bool mute)
{
    if (haudio == NULL)
    {
        return AUDIO_ERROR;
    }

    if (haudio->Mute == NULL)
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_PORT_NOT_BOUND,
                          AUDIO_BUS_OK,
                          AUDIO_STATE_ERROR);
    }

    if (haudio->State != AUDIO_STATE_READY)
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_NOT_READY,
                          AUDIO_BUS_OK,
                          haudio->State);
    }

    Audio_StatusTypeDef mute_status = haudio->Mute(haudio->MuteContext, mute);
    if(mute_status != AUDIO_OK)
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_MUTE,
                          AUDIO_BUS_OK,
                          AUDIO_STATE_ERROR);
    }

    audio_clear_error(haudio);
    haudio->IsMuted = mute;
    return mute_status;
}

Audio_StatusTypeDef Audio_Init(Audio_HandleTypeDef *haudio)
{
    // Initialize audio hardware
    if (haudio == NULL)
    {
        return AUDIO_ERROR; // Handle the error appropriately
    }

    haudio->State = AUDIO_STATE_RESET; //尚未初始化
    audio_clear_error(haudio);

    /*
     * Init 是设备配置的唯一装配入口：每次调用都恢复本驱动的默认地址和
     * 启动策略，而不是要求 Board 直接填写 Handle 内部字段。
     */

    if (haudio->BusOps == NULL 
        || haudio->BusOps->Prepare == NULL 
        || haudio->BusOps->Transmit == NULL
        || haudio->BusContext == NULL
        || haudio->Mute == NULL)
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_PORT_NOT_BOUND,
                          AUDIO_BUS_OK,
                          AUDIO_STATE_ERROR);
    }    

    haudio->State = AUDIO_STATE_BUSY;

    Audio_BusStatusTypeDef bus_status =
        haudio->BusOps->Prepare(haudio->BusContext);

    if(bus_status != AUDIO_BUS_OK)
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_BUS_PREPARE,
                          bus_status,
                          AUDIO_STATE_ERROR);
    }

    if(haudio->Mute(haudio->MuteContext, true) != AUDIO_OK)
    {
        return audio_fail(haudio,
                          AUDIO_ERROR_MUTE,
                          AUDIO_BUS_OK,
                          AUDIO_STATE_ERROR);
    }

    audio_clear_error(haudio);
    haudio->IsMuted = true;
    haudio->State = AUDIO_STATE_READY;
    return AUDIO_OK;
}
