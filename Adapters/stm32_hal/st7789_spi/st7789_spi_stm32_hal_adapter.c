/**
  ******************************************************************************
  * @file    st7789_spi_stm32_hal_adapter.c
  * @brief   STM32 HAL SPI/GPIO 到 ST7789 PortOps 的 Adapter 实现。
  *
  * @details
  *          本 Adapter 只负责 HAL 状态归一化、SPI 阻塞传输和 CS/D-C/RESET
  *          电平映射。SPI 实例、GPIO 和有效电平均由 Platform 注入，ST7789
  *          Device 不需要认识 STM32 HAL。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/st7789_spi/st7789_spi_stm32_hal_adapter.h"

#include <limits.h>
#include <stddef.h>

/**
  * @brief  将 STM32 HAL 状态转换为 ST7789 Device 可理解的端口状态。
  * @param  native_status HAL_StatusTypeDef 的整数表示。
  * @retval ST7789_PortStatusTypeDef 归一化后的端口结果。
  */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_status(int32_t native_status)
{
    switch ((HAL_StatusTypeDef)native_status)
    {
        case HAL_OK:
            return ST7789_PORT_OK;

        case HAL_BUSY:
            return ST7789_PORT_BUSY;

        case HAL_TIMEOUT:
            return ST7789_PORT_TIMEOUT;

        case HAL_ERROR:
        default:
            return ST7789_PORT_ERROR;
    }
}

/**
  * @brief  返回给定有效电平对应的非有效电平。
  * @param  active_state 信号有效时的平台电平。
  * @retval GPIO_PIN_RESET 或 GPIO_PIN_SET 的相反电平。
  */
static GPIO_PinState st7789_spi_stm32_hal_inactive_state(GPIO_PinState active_state)
{
    return (active_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

/**
  * @brief  通过当前 Adapter 控制 LCD 的片选有效或释放。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  asserted true 选中 LCD，false 释放 LCD。
  * @retval None
  */
static void st7789_spi_stm32_hal_set_chip_select(void *context, bool asserted)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    HAL_GPIO_WritePin(adapter->ChipSelectPort,
                      adapter->ChipSelectPin,
                      asserted
                          ? adapter->ChipSelectActiveState
                          : st7789_spi_stm32_hal_inactive_state(
                                adapter->ChipSelectActiveState));
}

/**
  * @brief  通过当前 Adapter 切换 LCD 的命令或数据模式。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  data_mode true 表示参数/像素数据，false 表示命令字节。
  * @retval None
  */
static void st7789_spi_stm32_hal_set_data_mode(void *context, bool data_mode)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    HAL_GPIO_WritePin(adapter->DataCommandPort,
                      adapter->DataCommandPin,
                      data_mode
                          ? st7789_spi_stm32_hal_inactive_state(adapter->CommandState)
                          : adapter->CommandState);
}

/**
  * @brief  通过当前 Adapter 控制 LCD 的硬件复位有效或释放。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  asserted true 使 RESET 有效，false 释放 RESET。
  * @retval None
  */
static void st7789_spi_stm32_hal_set_reset(void *context, bool asserted)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    HAL_GPIO_WritePin(adapter->ResetPort,
                      adapter->ResetPin,
                      asserted
                          ? adapter->ResetAssertState
                          : st7789_spi_stm32_hal_inactive_state(
                                adapter->ResetAssertState));
}

/**
  * @brief  通过 SPI 以 8 位串行帧同步写入命令或数据。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  data 待发送字节。
  * @param  length 待发送字节数。
  * @retval ST7789_PortStatusTypeDef 归一化后的 HAL SPI 结果。
  */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_write(
    void *context,
    const uint8_t *data,
    uint32_t length)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) ||
        (adapter->SPIHandle == NULL) ||
        (data == NULL) ||
        (length == 0u) ||
        (length > UINT16_MAX))
    {
        return ST7789_PORT_ERROR;
    }

    return st7789_spi_stm32_hal_status((int32_t)HAL_SPI_Transmit(adapter->SPIHandle,
                                                                   data,
                                                                   (uint16_t)length,
                                                                   adapter->TimeoutMs));
}

/**
  * @brief  通过 SPI 同步发送 dummy 字节并采样 LCD SDO 返回位流。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  transmit_data 用于产生读时钟的字节。
  * @param  receive_data 接收 SDO 位流的调用者缓冲区。
  * @param  length 全双工传输的字节数。
  * @retval ST7789_PortStatusTypeDef 归一化后的 HAL SPI 结果。
  */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_transfer(
    void *context,
    const uint8_t *transmit_data,
    uint8_t *receive_data,
    uint32_t length)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) ||
        (adapter->SPIHandle == NULL) ||
        (transmit_data == NULL) ||
        (receive_data == NULL) ||
        (length == 0u) ||
        (length > UINT16_MAX))
    {
        return ST7789_PORT_ERROR;
    }

    return st7789_spi_stm32_hal_status(
        (int32_t)HAL_SPI_TransmitReceive(adapter->SPIHandle,
                                          transmit_data,
                                          receive_data,
                                          (uint16_t)length,
                                          adapter->TimeoutMs));
}

/**
  * @brief  委托 STM32 HAL 的毫秒延时实现 ST7789 上电与复位等待。
  * @param  context 当前未使用，保留以满足 ST7789_PortDelayMsFunc。
  * @param  delay_ms 等待时间，单位为毫秒。
  * @retval None
  */
static void st7789_spi_stm32_hal_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    HAL_Delay(delay_ms);
}

/** @brief STM32 HAL SPI/GPIO 实现的 ST7789 PortOps 操作表。 */
static const ST7789_PortOpsTypeDef st7789_spi_stm32_hal_ops = {
    .SetChipSelect = st7789_spi_stm32_hal_set_chip_select,
    .SetDataMode = st7789_spi_stm32_hal_set_data_mode,
    .SetReset = st7789_spi_stm32_hal_set_reset,
    .Write = st7789_spi_stm32_hal_write,
    .Transfer = st7789_spi_stm32_hal_transfer,
    .DelayMs = st7789_spi_stm32_hal_delay_ms
};

/**
  * @brief  将 STM32 HAL SPI/GPIO Adapter 安装到 ST7789 Device Handle。
  * @param  hst7789 待绑定的 ST7789 Device Handle。
  * @param  adapter 由 Platform 长期持有的具体 SPI/GPIO Context。
  * @retval ST7789_OK PortOps 与 Context 已成对安装。
  * @retval ST7789_ERROR Handle、Context、GPIO、SPI 或超时参数无效。
  * @note   本函数只完成依赖装配，不会改变 CS、D/C、RESET 或启动 SPI 传输。
  */
ST7789_StatusTypeDef ST7789_SPI_STM32HALAdapter_Bind(
    ST7789_HandleTypeDef *hst7789,
    ST7789_SPI_STM32HALAdapterTypeDef *adapter)
{
    if ((hst7789 == NULL) ||
        (adapter == NULL) ||
        (adapter->SPIHandle == NULL) ||
        (adapter->ChipSelectPort == NULL) ||
        (adapter->ChipSelectPin == 0u) ||
        (adapter->DataCommandPort == NULL) ||
        (adapter->DataCommandPin == 0u) ||
        (adapter->ResetPort == NULL) ||
        (adapter->ResetPin == 0u) ||
        (adapter->TimeoutMs == 0u))
    {
        return ST7789_ERROR;
    }

    hst7789->PortOps = &st7789_spi_stm32_hal_ops;
    hst7789->PortContext = adapter;
    return ST7789_OK;
}
