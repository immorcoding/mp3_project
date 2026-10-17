/**
  ******************************************************************************
  * @file    audio_i2s_stm32_hal_adapter.c
  * @brief   STM32 HAL I2S/GPIO 到 Audio Device Interface 的 Adapter。
  *
  * @details
  *          本文件实现 I2S 准备、按采样率重新初始化、循环 DMA 输出、DMA 流使能位
  *          查询、硬件静音和 HAL 状态归一化。具体 I2S Handle 与静音 GPIO 由
  *          Platform 通过 Context 注入。
  *
  *          I2S 的 DMA 流（循环、半传输中断）与 NVIC 由 CubeMX 导出并挂在
  *          I2S Handle 的 hdmatx 上；本文件只在运行时检查它是否存在，不引用
  *          生成的 DMA Handle 符号，因此未导出 DMA 时仍可编译，Start 会被拒绝。
  *
  *          USE_HAL_I2S_REGISTER_CALLBACKS 为 0，HAL 只提供弱全局回调；本文件
  *          是 TxHalfCplt、TxCplt、Error 三个符号的唯一所有者，并转成强类型的
  *          Audio_StreamEventTypeDef 交给 Platform 注入的处理函数。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/audio_i2s/audio_i2s_stm32_hal_adapter.h"

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
  * @brief 当前唯一已绑定的 Adapter Context。
  * @note  HAL 全局 I2S 回调只带 I2S Handle，没有用户指针；本板只有一路 I2S2
  *        输出，Bind 时登记该 Context，回调再按 I2S Handle 校验匹配。
  */
static AudioI2S_STM32HALAdapterTypeDef * volatile haudio_i2s_active_adapter;

/**
  * @brief  按采样率重新初始化 I2S（PLL3 不动，只重算 I2S 分频）。
  * @param  context 必须指向当前 AudioI2S_STM32HALAdapterTypeDef 实例。
  * @param  sample_rate 目标采样率，单位 Hz。
  * @retval Audio_BusStatusTypeDef 归一化后的 HAL 结果。
  * @note   DeInit/Init 会经 CubeMX 生成的 MspDeInit/MspInit 重建 DMA 流，
  *         因此同时承担 Stop 失败后的 DMA 复位。
  */
static Audio_BusStatusTypeDef audio_i2s_stm32_hal_configure(void *context,
                                                            uint32_t sample_rate)
{
    AudioI2S_STM32HALAdapterTypeDef *adapter =
        (AudioI2S_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) || (adapter->I2SHandle == NULL) || (sample_rate == 0U))
    {
        return AUDIO_BUS_ERROR;
    }

    if (HAL_I2S_DeInit(adapter->I2SHandle) != HAL_OK)
    {
        return AUDIO_BUS_ERROR;
    }

    adapter->I2SHandle->Init.AudioFreq = sample_rate;

    return audio_i2s_stm32_hal_result((int32_t)HAL_I2S_Init(adapter->I2SHandle));
}

/**
  * @brief  启动整个循环缓冲区的 I2S DMA 输出。
  * @param  context 必须指向当前 AudioI2S_STM32HALAdapterTypeDef 实例。
  * @param  buffer 循环缓冲区。
  * @param  item_count 缓冲区的 16 位数据单元数量，HAL 限制不超过 65535。
  * @retval Audio_BusStatusTypeDef 归一化后的启动结果。
  * @note   DMA 流未由 CubeMX 导出（hdmatx 为空）时返回 AUDIO_BUS_ERROR。
  */
static Audio_BusStatusTypeDef audio_i2s_stm32_hal_start_circular(
    void *context,
    const int16_t *buffer,
    uint32_t item_count)
{
    AudioI2S_STM32HALAdapterTypeDef *adapter =
        (AudioI2S_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) ||
        (adapter->I2SHandle == NULL) ||
        (adapter->I2SHandle->hdmatx == NULL) ||
        (buffer == NULL) ||
        (item_count == 0U) ||
        (item_count > UINT16_MAX))
    {
        return AUDIO_BUS_ERROR;
    }

    HAL_StatusTypeDef status = HAL_I2S_Transmit_DMA(adapter->I2SHandle,
                                                    (const uint16_t *)buffer,
                                                    (uint16_t)item_count);
    return audio_i2s_stm32_hal_result((int32_t)status);
}

/**
  * @brief  中止 I2S DMA 输出并关闭 I2S 外设。
  * @param  context 必须指向当前 AudioI2S_STM32HALAdapterTypeDef 实例。
  * @retval Audio_BusStatusTypeDef HAL 返回码的归一化结果，不作为停止判据。
  */
static Audio_BusStatusTypeDef audio_i2s_stm32_hal_abort(void *context)
{
    AudioI2S_STM32HALAdapterTypeDef *adapter =
        (AudioI2S_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) || (adapter->I2SHandle == NULL))
    {
        return AUDIO_BUS_ERROR;
    }

    return audio_i2s_stm32_hal_result((int32_t)HAL_I2S_DMAStop(adapter->I2SHandle));
}

/**
  * @brief  读取 I2S 发送 DMA 流的 EN 位。
  * @param  context 必须指向当前 AudioI2S_STM32HALAdapterTypeDef 实例。
  * @retval true DMA 流仍使能。
  * @retval false DMA 流已停止，或 DMA 流未配置。
  */
static bool audio_i2s_stm32_hal_is_stream_active(void *context)
{
    AudioI2S_STM32HALAdapterTypeDef *adapter =
        (AudioI2S_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) ||
        (adapter->I2SHandle == NULL) ||
        (adapter->I2SHandle->hdmatx == NULL))
    {
        return false;
    }

    const DMA_Stream_TypeDef *stream =
        (const DMA_Stream_TypeDef *)adapter->I2SHandle->hdmatx->Instance;

    return (stream != NULL) && ((stream->CR & DMA_SxCR_EN) != 0U);
}

/**
  * @brief  安装或清除 ISR 事件处理函数。
  * @param  context 必须指向当前 AudioI2S_STM32HALAdapterTypeDef 实例。
  * @param  handler 事件处理函数，NULL 表示清除。
  * @param  handler_context 原样传给 handler 的上下文。
  * @note   在短临界区内成对写入，避免 ISR 读到新函数配旧上下文。
  */
static void audio_i2s_stm32_hal_set_event_handler(void *context,
                                                  Audio_StreamEventFunc handler,
                                                  void *handler_context)
{
    AudioI2S_STM32HALAdapterTypeDef *adapter =
        (AudioI2S_STM32HALAdapterTypeDef *)context;

    if (adapter == NULL)
    {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    adapter->EventHandler = handler;
    adapter->EventContext = handler_context;

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
  * @brief  把一个 HAL 回调事件分发给当前 Adapter 已安装的处理函数。
  * @param  hi2s HAL 回调传入的 I2S Handle。
  * @param  event 已归类的循环输出事件。
  * @note   ISR 上下文；Handle 不匹配（非本板音频 I2S）时忽略。
  */
static void audio_i2s_stm32_hal_dispatch(const I2S_HandleTypeDef *hi2s,
                                         Audio_StreamEventTypeDef event)
{
    AudioI2S_STM32HALAdapterTypeDef *adapter = haudio_i2s_active_adapter;

    if ((adapter == NULL) || (adapter->I2SHandle != hi2s))
    {
        return;
    }

    Audio_StreamEventFunc handler = adapter->EventHandler;

    if (handler != NULL)
    {
        handler(adapter->EventContext, event);
    }
}

/** @brief HAL 弱符号覆盖：DMA 读完前半区（半区 0）。 */
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    audio_i2s_stm32_hal_dispatch(hi2s, AUDIO_STREAM_EVENT_HALF0_RELEASED);
}

/** @brief HAL 弱符号覆盖：DMA 读完后半区（半区 1）。 */
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    audio_i2s_stm32_hal_dispatch(hi2s, AUDIO_STREAM_EVENT_HALF1_RELEASED);
}

/** @brief HAL 弱符号覆盖：I2S 或 DMA 传输错误。 */
void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
    audio_i2s_stm32_hal_dispatch(hi2s, AUDIO_STREAM_EVENT_ERROR);
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
    .Prepare = audio_i2s_stm32_hal_prepare,
    .Configure = audio_i2s_stm32_hal_configure,
    .StartCircular = audio_i2s_stm32_hal_start_circular,
    .Abort = audio_i2s_stm32_hal_abort,
    .IsStreamActive = audio_i2s_stm32_hal_is_stream_active,
    .SetEventHandler = audio_i2s_stm32_hal_set_event_handler
};

/**
  * @brief  将 STM32 HAL I2S 和 GPIO 静音 Adapter 安装到 Audio Device Handle。
  * @param  haudio 待绑定的 Audio Device Handle。
  * @param  adapter Platform 长期持有的具体 I2S/GPIO Adapter Context。
  * @retval AUDIO_OK Ops 和 Context 已成对安装。
  * @retval AUDIO_ERROR 句柄、Context 或必需的 Vendor 引用无效。
  * @note   本函数只完成依赖装配并登记 HAL 回调目标，不启动 DMA，也不改变 XSMT 电平。
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

    adapter->EventHandler = NULL;
    adapter->EventContext = NULL;
    haudio_i2s_active_adapter = adapter;

    return AUDIO_OK;
}
