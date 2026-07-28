/**
  ******************************************************************************
  * @file    audio.c
  * @brief   Audio Device 生命周期、同步发送、静音控制和错误诊断实现。
  ******************************************************************************
  */

#include "Components/audio/audio.h"
#include <stdbool.h>

/**
  * @brief  清除 Audio Handle 中保存的最近一次错误诊断。
  * @param  haudio Audio Device Handle。
  * @retval None
  */
static void audio_clear_error(Audio_HandleTypeDef *haudio)
{
    haudio->ErrorCode = AUDIO_ERROR_NONE;
    haudio->LastBusStatus = AUDIO_BUS_OK;
}

/**
  * @brief  统一保存 Audio Device 失败阶段、总线状态和后续生命周期状态。
  * @param  haudio Audio Device Handle。
  * @param  error Device 层失败原因。
  * @param  bus_status 归一化后的底层总线状态。
  * @param  next_state 失败后写入 Handle 的生命周期状态。
  * @retval AUDIO_ERROR
  */
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

/**
  * @brief  通过已绑定的 Audio Bus Adapter 同步发送 PCM 数据。
  * @param  haudio Audio Device Handle。
  * @param  data PCM 数据缓冲区。
  * @param  size 待发送的 16 位数据数量。
  * @retval AUDIO_OK 发送成功，Handle 返回 READY。
  * @retval AUDIO_ERROR 参数、绑定、状态或底层发送失败。
  */
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

    Audio_BusStatusTypeDef result = haudio->BusOps->Transmit(haudio->BusContext,
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

/**
  * @brief  通过已绑定的静音 Adapter 设置音频静音状态。
  * @param  haudio Audio Device Handle。
  * @param  mute true 表示静音，false 表示解除静音。
  * @retval AUDIO_OK 静音状态设置成功。
  * @retval AUDIO_ERROR Handle、绑定、生命周期状态或静音操作无效。
  */
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

/**
  * @brief  校验 Audio Adapter 绑定、准备音频总线并以静音状态进入 READY。
  * @param  haudio 已由 Port 安装 BusOps、BusContext 和 Mute 回调的 Handle。
  * @retval AUDIO_OK 初始化成功。
  * @retval AUDIO_ERROR Handle 无效、Port 未完整绑定、总线准备或静音失败。
  */
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
     * 启动策略，而不是要求 Platform 直接填写 Handle 内部字段。
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

    Audio_BusStatusTypeDef bus_status = haudio->BusOps->Prepare(haudio->BusContext);

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
