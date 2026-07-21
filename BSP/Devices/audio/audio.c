#include "audio.h"
#include <stddef.h>
#include <stdint.h>

Audio_StatusTypeDef Audio_Transmit(Audio_HandleTypeDef *haudio, const uint16_t *data, uint16_t size)
{
    if (haudio == NULL || data == NULL || size <= 0)
    {
        return AUDIO_ERROR; // Handle the error appropriately
    }

    if (haudio->BusOps == NULL || haudio->BusOps->Transmit == NULL)
    {
        return AUDIO_ERROR; // Handle the error appropriately
    }

    Audio_BusStateTypeDef result = haudio->BusOps->Transmit(haudio->BusContext, data, size);

    if (result != AUDIO_BUS_OK)
    {
        return AUDIO_ERROR; // Handle the error appropriately
    }

    return AUDIO_OK;
}

Audio_StatusTypeDef Audio_Init(Audio_HandleTypeDef *haudio)
{
    // Initialize audio hardware
    if (haudio == NULL)
    {
        return AUDIO_ERROR; // Handle the error appropriately
    }

    /*
     * Init 是设备配置的唯一装配入口：每次调用都恢复本驱动的默认地址和
     * 启动策略，而不是要求 Board 直接填写 Handle 内部字段。
     */

    if(haudio->BusOps == NULL || haudio->BusOps->Prepare == NULL || haudio->BusOps->Transmit == NULL)
    {
        return AUDIO_ERROR; // Handle the error appropriately
    }

    haudio->State = AUDIO_BUS_STATE_RESET; //尚未初始化

    if(haudio->BusOps->Prepare(haudio->BusContext) != AUDIO_BUS_OK)
    {
        return AUDIO_ERROR; // Handle the error appropriately
    }

    haudio->State = AUDIO_BUS_STATE_READY;
    return AUDIO_OK;
}