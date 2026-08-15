/**
  ******************************************************************************
  * @file    ft6x36_i2c_stm32_hal_adapter.h
  * @brief   STM32 HAL I2C/GPIO 到 FT6X36 Device Interface 的 Adapter。
  ******************************************************************************
  */

#ifndef FT6X36_I2C_STM32_HAL_ADAPTER_H
#define FT6X36_I2C_STM32_HAL_ADAPTER_H

#include <stdint.h>

#include "Components/ft6x36/ft6x36.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    I2C_HandleTypeDef *I2CHandle;
    GPIO_TypeDef *ResetPort;
    uint16_t ResetPin;
    GPIO_PinState ResetAssertState;
    uint32_t ProbeTrials;
    uint32_t TimeoutMs;
} FT6X36_I2C_STM32HALAdapterTypeDef;

FT6X36_StatusTypeDef FT6X36_I2C_STM32HALAdapter_Bind(
    FT6X36_HandleTypeDef *hft6x36,
    FT6X36_I2C_STM32HALAdapterTypeDef *adapter);

#endif /* FT6X36_I2C_STM32_HAL_ADAPTER_H */
