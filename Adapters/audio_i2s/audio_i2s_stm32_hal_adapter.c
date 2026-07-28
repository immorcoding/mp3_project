/**
  ******************************************************************************
  * @file    audio_i2s_stm32_hal_adapter.c
  * @brief   STM32 HAL I2S/GPIO 到 Audio Device Interface 的 Adapter。
  *
  * @details
  *          本文件实现 I2S 准备、阻塞发送、硬件静音和 HAL 状态归一化。
  *          具体 I2S Handle 与静音 GPIO 由 Platform 通过 Context 注入。
  ******************************************************************************
  */

#include "Adapters/audio_i2s/audio_i2s_stm32_hal_adapter.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**
  * @brief  将 STM32 HAL 状态转换为 Audio Device 可理解的总线状态。
  * @param  native_status HAL_StatusTypeDef 的整数表示。
  * @retval Audio_BusStatusTypeDef 归一化后的总线结果。
  */
static Audio_BusStatusTypeDef audio_i2s_stm32_hal_result(int32_t native_status)
{
    HAL_StatusTypeDef status = (HAL_StatusTypeDef)native_status;

    switch (status)
    {
        case HAL_OK:
            return AUDIO_BUS_OK;

        case HAL_ERROR:
            return AUDIO_BUS_ERROR;

        case HAL_BUSY:
            return AUDIO_BUS_BUSY;

        case HAL_TIMEOUT:
            return AUDIO_BUS_TIMEOUT;

        default:
            /* 未知 Vendor 状态不能泄漏到 Device，统一归为普通总线错误。 */
            return AUDIO_BUS_ERROR;
    }
}

/**
  * @brief  检查当前 I2S Handle 是否已经由 CubeMX 初始化并处于 READY。
  * @param  context 必须指向当前 AudioI2S_STM32HALAdapterTypeDef 实例。
  * @retval AUDIO_BUS_OK I2S 已就绪。
  * @retval AUDIO_BUS_ERROR Context 无效或 I2S 未就绪。
  */
static Audio_BusStatusTypeDef audio_i2s_stm32_hal_prepare(void *context)
{
    AudioI2S_STM32HALAdapterTypeDef *adapter =
        (AudioI2S_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) || (adapter->I2SHandle == NULL))
    {
        return AUDIO_BUS_ERROR;
    }

    if (HAL_I2S_GetState(adapter->I2SHandle) != HAL_I2S_STATE_READY)
    {
        return AUDIO_BUS_ERROR;
    }

    return AUDIO_BUS_OK;
}

/**
  * @brief  通过当前 I2S Adapter 同步发送一段 PCM 数据。
  * @param  context 必须指向当前 AudioI2S_STM32HALAdapterTypeDef 实例。
  * @param  data PCM 数据缓冲区。
  * @param  size 待发送的 16 位数据数量。
  * @retval Audio_BusStatusTypeDef 归一化后的 I2S 发送结果。
  */
static Audio_BusStatusTypeDef audio_i2s_stm32_hal_transmit(
    void *context,
    const uint16_t *data,
    uint16_t size)
{
    AudioI2S_STM32HALAdapterTypeDef *adapter =
        (AudioI2S_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) ||
        (adapter->I2SHandle == NULL) ||
        (data == NULL) ||
        (size == 0U))
    {
        return AUDIO_BUS_ERROR;
    }

    HAL_StatusTypeDef status = HAL_I2S_Transmit(adapter->I2SHandle,
                                                data,
                                                size,
                                                1000U);
    return audio_i2s_stm32_hal_result((int32_t)status);
}

/**
  * @brief  通过 PCM_XSMT GPIO 控制 PCM5102A 静音状态。
  * @param  context 必须指向当前 AudioI2S_STM32HALAdapterTypeDef 实例。
  * @param  mute true 拉低 XSMT，false 拉高 XSMT。
  * @retval AUDIO_OK GPIO 状态已写入。
  */
static Audio_StatusTypeDef audio_i2s_stm32_hal_mute(void *context, bool mute)
{
    AudioI2S_STM32HALAdapterTypeDef *adapter =
        (AudioI2S_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) ||
        (adapter->MutePort == NULL) ||
        (adapter->MutePin == 0U))
    {
        return AUDIO_ERROR;
    }

    HAL_GPIO_WritePin(adapter->MutePort,
                      adapter->MutePin,
                      mute ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return AUDIO_OK;
}

/**
  * @brief Audio Device 使用的 STM32 HAL I2S 操作表。
  * @note  表本身由 Adapter 持有，具体 I2S 实例通过 BusContext 注入。
  */
static const Audio_BusOpsTypeDef audio_i2s_stm32_hal_bus_ops = {
    .Transmit = audio_i2s_stm32_hal_transmit,
    .Prepare = audio_i2s_stm32_hal_prepare
};

/**
  * @brief  将 STM32 HAL I2S 和 GPIO 静音 Adapter 安装到 Audio Device Handle。
  * @param  haudio 待绑定的 Audio Device Handle。
  * @param  adapter Platform 长期持有的具体 I2S/GPIO Adapter Context。
  * @retval AUDIO_OK Ops 和 Context 已成对安装。
  * @retval AUDIO_ERROR 句柄、Context 或必需的 Vendor 引用无效。
  * @note   本函数只完成依赖装配，不发送 PCM 数据，也不改变 XSMT 电平。
  */
Audio_StatusTypeDef AudioI2S_STM32HALAdapter_Bind(
    Audio_HandleTypeDef *haudio,
    AudioI2S_STM32HALAdapterTypeDef *adapter)
{
    if ((haudio == NULL) ||
        (adapter == NULL) ||
        (adapter->I2SHandle == NULL) ||
        (adapter->MutePort == NULL) ||
        (adapter->MutePin == 0U))
    {
        return AUDIO_ERROR;
    }

    haudio->BusOps = &audio_i2s_stm32_hal_bus_ops;
    haudio->Mute = &audio_i2s_stm32_hal_mute;

    /* 同一 Context 同时提供 I2S 传输和当前硬件静音 GPIO。 */
    haudio->BusContext = adapter;
    haudio->MuteContext = adapter;

    return AUDIO_OK;
}
