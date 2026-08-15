/**
  ******************************************************************************
  * @file    temp_stm32_hal_adapter.h
  * @brief   STM32H7 内部温度传感器 HAL Adapter Interface。
  ******************************************************************************
  */

#ifndef TEMP_STM32_HAL_ADAPTER_H
#define TEMP_STM32_HAL_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

bool Temp_STM32HAL_Calibrate(ADC_HandleTypeDef *hadc);
bool Temp_STM32HAL_Read(ADC_HandleTypeDef *hadc, int32_t *temperature_mC);

#endif // TEMP_STM32_HAL_ADAPTER_H
