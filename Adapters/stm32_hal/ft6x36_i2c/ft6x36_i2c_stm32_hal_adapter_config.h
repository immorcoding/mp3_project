/**
  ******************************************************************************
  * @file    ft6x36_i2c_stm32_hal_adapter_config.h
  * @brief   当前 STM32 HAL FT6X36 I2C Adapter 的初始化时序参数。
  ******************************************************************************
  */

#ifndef FT6X36_I2C_STM32_HAL_ADAPTER_CONFIG_H
#define FT6X36_I2C_STM32_HAL_ADAPTER_CONFIG_H

#define FT6X36_I2C_STM32HAL_RESET_ASSERT_DELAY_MS    5u /* TP_RST 逻辑断言后的最小保持时间，单位为毫秒。 */
#define FT6X36_I2C_STM32HAL_RESET_RELEASE_DELAY_MS   200u /* TP_RST 逻辑释放后、开始 I2C 探测前的最小稳定时间，单位为毫秒。 */

#endif /* FT6X36_I2C_STM32_HAL_ADAPTER_CONFIG_H */
