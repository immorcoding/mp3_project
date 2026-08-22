/**
  ******************************************************************************
  * @file    led_gpio_stm32_hal_adapter.h
  * @brief   STM32 HAL GPIO LED Adapter 的 Context 与绑定 Interface。
  ******************************************************************************
  */

#ifndef LED_GPIO_STM32_HAL_ADAPTER_H
#define LED_GPIO_STM32_HAL_ADAPTER_H

#include <stdint.h>

#include "Components/led/led.h"
#include "stm32h7xx_hal.h"

/**
 * @brief STM32 HAL GPIO LED Adapter 所需的板级 Context。
 * @note  Context 由 Platform 长期持有。OnState 定义逻辑 ON 对应的物理电平，
 *        因而高有效与低有效差异不会泄漏到 LED Device 或上层调用者。
 */
typedef struct
{
    GPIO_TypeDef *Port;
    uint16_t Pin;
    GPIO_PinState OnState;
} LED_GPIO_STM32HALAdapterTypeDef;

LED_StatusTypeDef LED_GPIO_STM32HALAdapter_Bind(
    LED_HandleTypeDef *hled,
    LED_GPIO_STM32HALAdapterTypeDef *adapter);

#endif /* LED_GPIO_STM32_HAL_ADAPTER_H */
