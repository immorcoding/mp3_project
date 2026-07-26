#include "Adapters/audio_stm32_i2s/audio_stm32_i2s_adapter.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**
  * @brief  将 STM32 HAL 状态转换为 Audio Device 可理解的总线状态。
  * @param  native_status HAL_StatusTypeDef 的整数表示。
  * @retval Audio_BusStatusTypeDef 归一化后的总线结果。
  */
static Audio_BusStatusTypeDef audio_stm32_i2s_adapter_result(int32_t native_status)
{
/*******自定义修改状态码*******/
    HAL_StatusTypeDef status = (HAL_StatusTypeDef)native_status;

    switch (status)
    {
        case HAL_OK:
            return AUDIO_BUS_OK; // Handle specific error case
        case HAL_ERROR:
            return AUDIO_BUS_ERROR; // Handle specific error case
        case HAL_BUSY:
            return AUDIO_BUS_BUSY; // Handle specific error case
        case HAL_TIMEOUT:
            return AUDIO_BUS_TIMEOUT; // Handle specific error case
        default:
            // Handle other errors
            return AUDIO_BUS_ERROR; //unknown error
    
    }
/*****************************/
}

/**
  * @brief  检查当前 I2S Handle 是否已经由 CubeMX 初始化并处于 READY。
  * @param  AudioContext 必须指向当前 I2S_HandleTypeDef 实例。
  * @retval AUDIO_BUS_OK I2S 已就绪。
  * @retval AUDIO_BUS_ERROR Context 无效或 I2S 未就绪。
  */
static Audio_BusStatusTypeDef audio_prepare(void *AudioContext)
{
    Audio_STM32I2SAdapterTypeDef *adapter = (Audio_STM32I2SAdapterTypeDef *)AudioContext;

    if ((adapter == NULL) || (adapter->I2SHandle == NULL))
    {
        return AUDIO_BUS_ERROR;
    }

/**************** 自定义的音频模块初始化函数 ****************/
    if (HAL_I2S_GetState(adapter->I2SHandle) != HAL_I2S_STATE_READY)
    {
        return AUDIO_BUS_ERROR;
    }
    return AUDIO_BUS_OK;
/****************************************************/
}

/**
  * @brief  通过当前 I2S Adapter 同步发送一段 PCM 数据。
  * @param  AudioContext 必须指向当前 I2S_HandleTypeDef 实例。
  * @param  data PCM 数据缓冲区。
  * @param  size 待发送的 16 位数据数量。
  * @retval Audio_BusStatusTypeDef 归一化后的 I2S 发送结果。
  */
static Audio_BusStatusTypeDef audio_transmit(void *AudioContext, const uint16_t *data, uint16_t size)
{
    Audio_STM32I2SAdapterTypeDef *adapter = (Audio_STM32I2SAdapterTypeDef *)AudioContext;

    if ((adapter == NULL) ||
        (adapter->I2SHandle == NULL) ||
        (data == NULL) ||
        (size == 0U))
    {
        return AUDIO_BUS_ERROR;
    }

/**************** 自定义的音频发送函数 ****************/
    HAL_StatusTypeDef status = HAL_I2S_Transmit(adapter->I2SHandle,
                                                data,
                                                size,
                                                1000U);
    return audio_stm32_i2s_adapter_result((int32_t)status);
/****************************************************/
}

/**
  * @brief  通过 PCM_XSMT GPIO 控制 PCM5102A 静音状态。
  * @param  MuteContext 当前 GPIO 实现不需要对象上下文。
  * @param  mute true 拉低 XSMT，false 拉高 XSMT。
  * @retval AUDIO_OK GPIO 状态已写入。
  */
static Audio_StatusTypeDef audio_mute(void *MuteContext, bool mute)
{
    Audio_STM32I2SAdapterTypeDef *adapter = (Audio_STM32I2SAdapterTypeDef *)MuteContext;

    if ((adapter == NULL) ||
        (adapter->MutePort == NULL) ||
        (adapter->MutePin == 0U))
    {
        return AUDIO_ERROR;
    }

/**************** 自定义的静音函数 ****************/
    HAL_GPIO_WritePin(adapter->MutePort,
                      adapter->MutePin,
                      mute ? GPIO_PIN_RESET : GPIO_PIN_SET);
/****************************************************/
    return AUDIO_OK;
}

static const Audio_BusOpsTypeDef audio_bus_ops = {
    .Transmit = audio_transmit,
    .Prepare = audio_prepare
};

/**
  * @brief  将本板 I2S2 和 PCM_XSMT Adapter 安装到 Audio Device Handle。
  * @param  haudio 待绑定的 Audio Device Handle。
  * @retval AUDIO_OK Ops 和 Context 已成对安装。
  * @retval AUDIO_ERROR haudio 为空。
  * @note   本函数只完成依赖装配，不发送 PCM 数据，也不改变 XSMT 电平。
  */
Audio_StatusTypeDef Audio_STM32I2SAdapter_Bind(
    Audio_HandleTypeDef *haudio,
    Audio_STM32I2SAdapterTypeDef *adapter)
{
    if ((haudio == NULL) ||
        (adapter == NULL) ||
        (adapter->I2SHandle == NULL) ||
        (adapter->MutePort == NULL) ||
        (adapter->MutePin == 0U))
    {
        return AUDIO_ERROR;
    }

    haudio->BusOps = &audio_bus_ops;
    haudio->Mute = &audio_mute;
/**************** 句柄绑定，由用户改动 ****************/
    haudio->BusContext = adapter;
    haudio->MuteContext = adapter;

    return AUDIO_OK;
}
