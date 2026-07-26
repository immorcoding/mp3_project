#include "Adapters/soft_i2c_stm32_gpio/soft_i2c_stm32_gpio_adapter.h"

#include <stddef.h>

/**
  * @brief 将组件的逻辑线条和逻辑状态映射到 STM32 HAL GPIO。
  * @param context STM32 GPIO Adapter 上下文。
  * @param line 待操作的 SCL 或 SDA。
  * @param state 主动拉低或释放开漏输出。
  * @retval None
  */
static void soft_i2c_stm32_gpio_write(void *context,
                                      SoftI2C_LineTypeDef line,
                                      SoftI2C_LineStateTypeDef state)
{
  SoftI2C_STM32GPIOAdapterTypeDef *adapter = (SoftI2C_STM32GPIOAdapterTypeDef *)context;
  GPIO_TypeDef *port = (line == SOFT_I2C_LINE_SCL)
      ? adapter->SCLPort
      : adapter->SDAPort;
  uint16_t pin = (line == SOFT_I2C_LINE_SCL)
      ? adapter->SCLPin
      : adapter->SDAPin;
  GPIO_PinState pin_state = (state == SOFT_I2C_LINE_LOW)
      ? GPIO_PIN_RESET
      : GPIO_PIN_SET;

  HAL_GPIO_WritePin(port, pin, pin_state);
}

/**
  * @brief 读取 STM32 GPIO 输入寄存器反映的实际线条电平。
  * @param context STM32 GPIO Adapter 上下文。
  * @param line 待读取的 SCL 或 SDA。
  * @retval true 线条为高电平。
  * @retval false 线条为低电平。
  */
static bool soft_i2c_stm32_gpio_read(const void *context,
                                     SoftI2C_LineTypeDef line)
{
  const SoftI2C_STM32GPIOAdapterTypeDef *adapter = (const SoftI2C_STM32GPIOAdapterTypeDef *)context;
  GPIO_TypeDef *port = (line == SOFT_I2C_LINE_SCL)
      ? adapter->SCLPort
      : adapter->SDAPort;
  uint16_t pin = (line == SOFT_I2C_LINE_SCL)
      ? adapter->SCLPin
      : adapter->SDAPin;

  return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
}

static const SoftI2C_GPIOOpsTypeDef soft_i2c_stm32_gpio_ops = {
    .Write = soft_i2c_stm32_gpio_write,
    .Read = soft_i2c_stm32_gpio_read
};

/**
  * @brief 把 STM32 GPIO Adapter 装配到一个 SoftI2C 实例。
  * @param hi2c 待装配的软件 I2C 句柄。
  * @param adapter 由 Platform 持有的 STM32 GPIO 上下文。
  * @param delay_cycles 每个时序阶段的忙等待循环次数。
  * @param clock_stretch_timeout 等待 SCL 释放的最大轮询次数。
  * @retval SOFT_I2C_OK 装配完成。
  * @retval SOFT_I2C_ERROR 参数无效。
  */
SoftI2C_StatusTypeDef SoftI2C_STM32GPIOAdapter_Bind(
    SoftI2C_HandleTypeDef *hi2c,
    SoftI2C_STM32GPIOAdapterTypeDef *adapter,
    uint32_t delay_cycles,
    uint32_t clock_stretch_timeout)
{
  if ((hi2c == NULL) ||
      (adapter == NULL) ||
      (adapter->SCLPort == NULL) ||
      (adapter->SCLPin == 0U) ||
      (adapter->SDAPort == NULL) ||
      (adapter->SDAPin == 0U) ||
      (clock_stretch_timeout == 0U))
  {
    return SOFT_I2C_ERROR;
  }

  hi2c->GPIOOps = &soft_i2c_stm32_gpio_ops;
  hi2c->GPIOContext = adapter;
  hi2c->DelayCycles = delay_cycles;
  hi2c->ClockStretchTimeout = clock_stretch_timeout;
  hi2c->State = SOFT_I2C_STATE_RESET;
  hi2c->ErrorCode = SOFT_I2C_ERROR_NONE;
  return SOFT_I2C_OK;
}
