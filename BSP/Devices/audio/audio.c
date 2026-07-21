#include "audio.h"
#include <stdbool.h>

Audio_StatusTypeDef Audio_Transmit(Audio_HandleTypeDef *haudio,
                                    const uint16_t *data,
                                    uint16_t size)
{
    if ((haudio == NULL) ||
        (data == NULL) ||
        (size == 0U))
    {
        return AUDIO_ERROR;
    }

    if ((haudio->BusOps == NULL) ||
        (haudio->BusOps->Transmit == NULL) ||
        (haudio->State != AUDIO_STATE_READY))
    {
        return AUDIO_ERROR;
    }

    haudio->State = AUDIO_STATE_BUSY;

    Audio_BusStateTypeDef result =
        haudio->BusOps->Transmit(haudio->BusContext,
                                 data,
                                 size);

    if (result != AUDIO_BUS_OK)
    {
        haudio->State = AUDIO_STATE_ERROR;
        return AUDIO_ERROR;
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
        return AUDIO_ERROR;
    }

    Audio_StatusTypeDef mute_status = haudio->Mute(haudio->MuteContext, mute);
    if(mute_status != AUDIO_OK)
    {
        return AUDIO_ERROR;
    }
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

    /*
     * Init 是设备配置的唯一装配入口：每次调用都恢复本驱动的默认地址和
     * 启动策略，而不是要求 Board 直接填写 Handle 内部字段。
     */

    if (haudio->BusOps == NULL 
        || haudio->BusOps->Prepare == NULL 
        || haudio->BusOps->Transmit == NULL
        || haudio->Mute == NULL)
    {
        haudio->State = AUDIO_STATE_ERROR;
        return AUDIO_ERROR; // Handle the error appropriately
    }    

    if(haudio->BusOps->Prepare(haudio->BusContext) != AUDIO_BUS_OK)
    {
        haudio->State = AUDIO_STATE_ERROR;
        return AUDIO_ERROR; // Handle the error appropriately
    }

    if(haudio->Mute(haudio->MuteContext, true) != AUDIO_OK)
    {
        haudio->State = AUDIO_STATE_ERROR;
        return AUDIO_ERROR; // Handle the error appropriately
    }

    haudio->IsMuted = true;
    haudio->State = AUDIO_STATE_READY;
    return AUDIO_OK;
}