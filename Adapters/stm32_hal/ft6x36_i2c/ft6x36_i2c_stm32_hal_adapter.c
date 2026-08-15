/**
  ******************************************************************************
  * @file    ft6x36_i2c_stm32_hal_adapter.c
  * @brief   STM32 HAL I2C 与 GPIO 到 FT6X36 PortOps 的 Adapter 实现。
  *
  * @details
  *          本 Module 将 HAL I2C 从机探测、8-bit 寄存器读、触摸复位 GPIO 和
  *          毫秒延时转换为 FT6X36 Device 所拥有的 PortOps。它不选择 I2C2、
  *          TP_RST 引脚或设备地址；这些板级对象由 Platform Touch 注入。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/ft6x36_i2c/ft6x36_i2c_stm32_hal_adapter.h"

#include <limits.h>
#include <stddef.h>

/**
  * @brief  将 STM32 HAL 返回状态转换为 FT6X36 的归一化端口状态。
  * @param  hal_status HAL I2C 调用的返回值。
  * @retval 对应的 FT6X36 端口状态。
  */
static FT6X36_PortStatusTypeDef ft6x36_i2c_stm32_hal_map_status(
    HAL_StatusTypeDef hal_status)
{
    switch (hal_status)
    {
        case HAL_OK:
            return FT6X36_PORT_OK;

        case HAL_BUSY:
            return FT6X36_PORT_BUSY;

        case HAL_TIMEOUT:
            return FT6X36_PORT_TIMEOUT;

        default:
            return FT6X36_PORT_ERROR;
    }
}

/**
  * @brief  驱动或释放 FT6X36 的硬件复位 GPIO。
  * @param  context 指向 Platform 长期持有的 Adapter Context。
  * @param  asserted true 表示使复位信号有效，false 表示释放复位。
  * @note   Adapter 只按 Context 中的有效电平翻译逻辑复位状态，不假设本板复位
  *         引脚一定低有效。
  */
static void ft6x36_i2c_stm32_hal_set_reset(void *context, bool asserted)
{
    FT6X36_I2C_STM32HALAdapterTypeDef *adapter = context;
    GPIO_PinState pin_state;

    if ((adapter == NULL) || (adapter->ResetPort == NULL))
    {
        return;
    }

    pin_state = asserted ? adapter->ResetAssertState :
                ((adapter->ResetAssertState == GPIO_PIN_RESET) ?
                 GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(adapter->ResetPort, adapter->ResetPin, pin_state);
}

/**
  * @brief  使用 STM32 HAL 时基等待触摸控制器的复位稳定时间。
  * @param  context 未使用，保留以匹配 FT6X36 PortOps。
  * @param  delay_ms 等待时长，单位为毫秒。
  */
static void ft6x36_i2c_stm32_hal_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    HAL_Delay(delay_ms);
}

/**
  * @brief  检查指定 7-bit I2C 地址上的 FT6X36 是否应答。
  * @param  context 指向 Platform 长期持有的 Adapter Context。
  * @param  address_7bit FT6X36 的 7-bit I2C 从机地址。
  * @retval 归一化的 I2C 探测结果。
  * @note   STM32 HAL 采用左移一位后的地址表达；转换只留在 Adapter 内，避免
  *         Component 和 Platform 混用 7-bit、8-bit 地址表示。
  */
static FT6X36_PortStatusTypeDef ft6x36_i2c_stm32_hal_is_ready(
    void *context,
    uint8_t address_7bit)
{
    FT6X36_I2C_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) ||
        (adapter->I2CHandle == NULL) ||
        (address_7bit > 0x7Fu) ||
        (adapter->ProbeTrials == 0u))
    {
        return FT6X36_PORT_ERROR;
    }

    return ft6x36_i2c_stm32_hal_map_status(
        HAL_I2C_IsDeviceReady(adapter->I2CHandle,
                              (uint16_t)address_7bit << 1u,
                              adapter->ProbeTrials,
                              adapter->TimeoutMs));
}

/**
  * @brief  从 FT6X36 的 8-bit 寄存器地址连续读取数据。
  * @param  context 指向 Platform 长期持有的 Adapter Context。
  * @param  address_7bit FT6X36 的 7-bit I2C 从机地址。
  * @param  register_address 起始 8-bit 寄存器地址。
  * @param  data 接收数据的有效缓冲区。
  * @param  length 要读取的字节数。
  * @retval 归一化的 HAL I2C 存储器读取结果。
  * @note   HAL 的传输长度为 uint16_t；本 Adapter 在调用前拒绝更大的长度，避免
  *         隐式截断。后续读取触点状态和坐标仍复用这一条连续寄存器读取路径。
  */
static FT6X36_PortStatusTypeDef ft6x36_i2c_stm32_hal_mem_read(
    void *context,
    uint8_t address_7bit,
    uint8_t register_address,
    uint8_t *data,
    uint32_t length)
{
    FT6X36_I2C_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) ||
        (adapter->I2CHandle == NULL) ||
        (address_7bit > 0x7Fu) ||
        (data == NULL) ||
        (length == 0u) ||
        (length > UINT16_MAX))
    {
        return FT6X36_PORT_ERROR;
    }

    return ft6x36_i2c_stm32_hal_map_status(
        HAL_I2C_Mem_Read(adapter->I2CHandle,
                         (uint16_t)address_7bit << 1u,
                         register_address,
                         I2C_MEMADD_SIZE_8BIT,
                         data,
                         (uint16_t)length,
                         adapter->TimeoutMs));
}

/** @brief FT6X36 Device 使用的 STM32 HAL I2C PortOps 表。 */
static const FT6X36_PortOpsTypeDef ft6x36_i2c_stm32_hal_ops = {
    .SetReset = ft6x36_i2c_stm32_hal_set_reset,
    .DelayMs = ft6x36_i2c_stm32_hal_delay_ms,
    .IsReady = ft6x36_i2c_stm32_hal_is_ready,
    .MemRead = ft6x36_i2c_stm32_hal_mem_read
};

/**
  * @brief  把 STM32 HAL I2C/GPIO Context 绑定到 FT6X36 Device。
  * @param  hft6x36 待绑定的 FT6X36 Handle。
  * @param  adapter 由 Platform 长期持有的 HAL I2C/GPIO Adapter Context。
  * @retval FT6X36_OK 绑定成功。
  * @retval FT6X36_ERROR 参数或必需 HAL/GPIO 对象无效。
  * @note   绑定不执行硬件访问。Platform 在 Context 中提供本板 I2C Handle、复位
  *         GPIO、有效极性和超时；FT6X36_Init() 才会实际执行复位和探测。
  */
FT6X36_StatusTypeDef FT6X36_I2C_STM32HALAdapter_Bind(
    FT6X36_HandleTypeDef *hft6x36,
    FT6X36_I2C_STM32HALAdapterTypeDef *adapter)
{
    if ((hft6x36 == NULL) ||
        (adapter == NULL) ||
        (adapter->I2CHandle == NULL) ||
        (adapter->ResetPort == NULL) ||
        (adapter->ProbeTrials == 0u))
    {
        return FT6X36_ERROR;
    }

    hft6x36->PortOps = &ft6x36_i2c_stm32_hal_ops;
    hft6x36->PortContext = adapter;
    hft6x36->State = FT6X36_STATE_RESET;
    hft6x36->ErrorCode = FT6X36_ERROR_NONE;
    hft6x36->LastPortStatus = FT6X36_PORT_OK;
    hft6x36->LastFailedRegister = 0u;

    return FT6X36_OK;
}
