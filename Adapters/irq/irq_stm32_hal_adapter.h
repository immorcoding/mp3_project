#ifndef IRQ_STM32_HAL_ADAPTER_H
#define IRQ_STM32_HAL_ADAPTER_H

#include <stdint.h>

/**
  * @brief STM32 HAL IRQ Adapter 操作结果。
  */
typedef enum
{
    IRQ_STM32_HAL_ADAPTER_OK = 0,
    IRQ_STM32_HAL_ADAPTER_ERROR
} IRQ_STM32HALAdapter_StatusTypeDef;

/**
  * @brief 接收 STM32 HAL 原始 GPIO EXTI 引脚的上行回调。
  * @note  回调运行在 ISR 上下文。
  */
typedef void (*IRQ_STM32HALAdapter_CallbackTypeDef)(uint16_t gpio_pin,
                                                     void *context);

/**
  * @brief STM32 HAL 全局 EXTI 入口当前绑定的回调和对象上下文。
  */
typedef struct
{
    IRQ_STM32HALAdapter_CallbackTypeDef Callback;
    void *Context;
} IRQ_STM32HALAdapterTypeDef;

IRQ_STM32HALAdapter_StatusTypeDef IRQ_STM32HALAdapter_Bind(
    IRQ_STM32HALAdapterTypeDef *adapter,
    IRQ_STM32HALAdapter_CallbackTypeDef callback,
    void *context);

IRQ_STM32HALAdapter_StatusTypeDef IRQ_STM32HALAdapter_Unbind(
    IRQ_STM32HALAdapterTypeDef *adapter);

#endif /* IRQ_STM32_HAL_ADAPTER_H */
