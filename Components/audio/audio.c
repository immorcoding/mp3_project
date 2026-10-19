/**
  ******************************************************************************
  * @file    audio.c
  * @brief   Audio Device 生命周期、循环输出控制、静音控制和错误诊断实现。
  ******************************************************************************
  */

#include "Components/audio/audio.h"
#include <stdbool.h>
#include <stddef.h>

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
  * @brief  判断 Handle 的总线绑定是否完整。
  * @param  haudio 非空 Audio Device Handle。
  * @retval true Ops 表、全部函数与 Context 均已绑定。
  */
static bool audio_bus_is_bound(const Audio_HandleTypeDef *haudio)
{
    return (haudio->BusOps != NULL) &&
           (haudio->BusOps->Prepare != NULL) &&
           (haudio->BusOps->Configure != NULL) &&
           (haudio->BusOps->StartCircular != NULL) &&
           (haudio->BusOps->Abort != NULL) &&
           (haudio->BusOps->IsStreamActive != NULL) &&
           (haudio->BusOps->SetEventHandler != NULL) &&
           (haudio->BusContext != NULL);
}

/**
  * @brief  按采样率重新初始化已绑定的音频总线。
  * @param  haudio Audio Device Handle。
  * @param  sample_rate 目标采样率，单位 Hz，不能为 0。
  * @retval AUDIO_OK 总线已按新采样率初始化，Handle 返回 READY。
  * @retval AUDIO_ERROR 参数、绑定、状态或总线重新初始化失败。
  * @note   READY 与 STREAMING 都允许调用：Stop 重试路径在中止失败后借此重置总线，
  *         成功后 Handle 一律回到 READY。
  */
Audio_StatusTypeDef Audio_Configure(Audio_HandleTypeDef *haudio, uint32_t sample_rate)
{
    if (haudio == NULL)
    {
        return AUDIO_ERROR;
    }

    if (sample_rate == 0U)
    {
        return audio_fail(haudio, AUDIO_ERROR_INVALID_PARAM, AUDIO_BUS_OK, haudio->State);
    }

    if (!audio_bus_is_bound(haudio))
    {
        return audio_fail(haudio, AUDIO_ERROR_PORT_NOT_BOUND, AUDIO_BUS_OK, AUDIO_STATE_ERROR);
    }

    if ((haudio->State != AUDIO_STATE_READY) && (haudio->State != AUDIO_STATE_STREAMING))
    {
        return audio_fail(haudio, AUDIO_ERROR_NOT_READY, AUDIO_BUS_OK, haudio->State);
    }

    audio_clear_error(haudio);

    Audio_BusStatusTypeDef result = haudio->BusOps->Configure(haudio->BusContext, sample_rate);

    if (result != AUDIO_BUS_OK)
    {
        /* 重新初始化可由调用者重试，故失败后仍回到 READY。 */
        return audio_fail(haudio, AUDIO_ERROR_BUS_CONFIGURE, result, AUDIO_STATE_READY);
    }

    haudio->State = AUDIO_STATE_READY;
    return AUDIO_OK;
}

/**
  * @brief  启动循环 DMA 输出。
  * @param  haudio Audio Device Handle。
  * @param  buffer 循环缓冲区，按 I2S 帧顺序排列的 16 位 PCM 数据。
  * @param  item_count 整个循环缓冲区的 16 位数据单元数量。
  * @retval AUDIO_OK 已启动，Handle 进入 STREAMING。
  * @retval AUDIO_ERROR 参数、绑定、状态或底层启动失败。
  */
Audio_StatusTypeDef Audio_StartCircular(Audio_HandleTypeDef *haudio,
                                        const int16_t *buffer,
                                        uint32_t item_count)
{
    if (haudio == NULL)
    {
        return AUDIO_ERROR;
    }

    if ((buffer == NULL) || (item_count == 0U))
    {
        return audio_fail(haudio, AUDIO_ERROR_INVALID_PARAM, AUDIO_BUS_OK, haudio->State);
    }

    if (!audio_bus_is_bound(haudio))
    {
        return audio_fail(haudio, AUDIO_ERROR_PORT_NOT_BOUND, AUDIO_BUS_OK, AUDIO_STATE_ERROR);
    }

    if (haudio->State != AUDIO_STATE_READY)
    {
        return audio_fail(haudio, AUDIO_ERROR_NOT_READY, AUDIO_BUS_OK, haudio->State);
    }

    audio_clear_error(haudio);

    Audio_BusStatusTypeDef result = haudio->BusOps->StartCircular(haudio->BusContext, buffer, item_count);

    if (result != AUDIO_BUS_OK)
    {
        /* 启动失败时 HAL 已回到空闲，调用者可重新 Configure 后重试。 */
        return audio_fail(haudio, AUDIO_ERROR_BUS_STREAM, result, AUDIO_STATE_READY);
    }

    haudio->State = AUDIO_STATE_STREAMING;
    return AUDIO_OK;
}

/**
  * @brief  中止循环 DMA 输出，并无条件把 Handle 放回 READY。
  * @param  haudio Audio Device Handle。
  * @retval AUDIO_OK 底层中止返回成功。
  * @retval AUDIO_ERROR 绑定无效或底层中止返回错误。
  * @note   返回值不是停止判据：调用者必须再用 Audio_IsStreamActive() 核实 DMA 流。
  *         因此即使底层报错，Handle 也回到 READY，以便重试路径继续 Configure。
  */
Audio_StatusTypeDef Audio_Abort(Audio_HandleTypeDef *haudio)
{
    if (haudio == NULL)
    {
        return AUDIO_ERROR;
    }

    if (!audio_bus_is_bound(haudio))
    {
        return audio_fail(haudio, AUDIO_ERROR_PORT_NOT_BOUND, AUDIO_BUS_OK, AUDIO_STATE_ERROR);
    }

    Audio_BusStatusTypeDef result = haudio->BusOps->Abort(haudio->BusContext);

    if (result != AUDIO_BUS_OK)
    {
        return audio_fail(haudio, AUDIO_ERROR_BUS_STREAM, result, AUDIO_STATE_READY);
    }

    audio_clear_error(haudio);
    haudio->State = AUDIO_STATE_READY;
    return AUDIO_OK;
}

/**
  * @brief  读取底层 DMA 流是否仍然使能。
  * @param  haudio Audio Device Handle。
  * @retval true DMA 流仍使能。
  * @retval false DMA 流已停止，或 Handle 未绑定。
  */
bool Audio_IsStreamActive(const Audio_HandleTypeDef *haudio)
{
    if ((haudio == NULL) || !audio_bus_is_bound(haudio))
    {
        return false;
    }

    return haudio->BusOps->IsStreamActive(haudio->BusContext);
}

/**
  * @brief  安装或清除 ISR 上下文的循环输出事件处理函数。
  * @param  haudio Audio Device Handle。
  * @param  handler 事件处理函数，NULL 表示清除。
  * @param  handler_context 原样传给 handler 的上下文。
  * @retval AUDIO_OK 已转交 Adapter。
  * @retval AUDIO_ERROR Handle 或绑定无效。
  */
Audio_StatusTypeDef Audio_SetStreamEventHandler(Audio_HandleTypeDef *haudio,
                                                Audio_StreamEventFunc handler,
                                                void *handler_context)
{
    if (haudio == NULL)
    {
        return AUDIO_ERROR;
    }

    if (!audio_bus_is_bound(haudio))
    {
        return audio_fail(haudio, AUDIO_ERROR_PORT_NOT_BOUND, AUDIO_BUS_OK, AUDIO_STATE_ERROR);
    }

    haudio->BusOps->SetEventHandler(haudio->BusContext, handler, handler_context);
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

    if ((haudio->State != AUDIO_STATE_READY) && (haudio->State != AUDIO_STATE_STREAMING))
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

    if (!audio_bus_is_bound(haudio) || (haudio->Mute == NULL))
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
