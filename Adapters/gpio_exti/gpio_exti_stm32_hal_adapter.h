/**
  ******************************************************************************
  * @file    gpio_exti_stm32_hal_adapter.h
  * @brief   STM32 HAL GPIO EXTI 回调对象和注册接口。
  *
  * @details
  *          本 Adapter 独占 STM32 HAL 的 HAL_GPIO_EXTI_Callback() 全局入口，
  *          仅按 GPIO 引脚位掩码分发外部中断事件。它不是通用 NVIC 中断框架：
  *          SDMMC、DMA、USB、UART 等外设中断应继续由对应 HAL IRQHandler 和
  *          具体外设模块处理，不能注册到本 Adapter。
  ******************************************************************************
  */

#ifndef GPIO_EXTI_STM32_HAL_ADAPTER_H
#define GPIO_EXTI_STM32_HAL_ADAPTER_H

#include <stdint.h>

/**
  * @brief STM32 HAL GPIO EXTI Adapter 操作结果。
  */
typedef enum
{
    GPIOEXTI_STM32HAL_ADAPTER_OK = 0, /**< 回调注册或注销成功。 */
    GPIOEXTI_STM32HAL_ADAPTER_ERROR  /**< 参数非法、重复注册或目标未注册。 */
} GPIOEXTI_STM32HALAdapter_StatusTypeDef;

/**
  * @brief 接收 STM32 HAL 原始 GPIO EXTI 引脚位掩码的中断处理函数。
  * @note  回调运行在 ISR 上下文；实现只能执行非阻塞、FromISR 安全的操作。
  */
typedef void (*GPIOEXTI_STM32HALAdapter_HandlerTypeDef)(uint16_t gpio_pin,
                                                         void *context);

/**
  * @brief 调用者持有的 GPIO EXTI 回调对象。
  * @note  本结构采用与 Zephyr gpio_callback 相似的侵入式链表设计。对象必须
  *        在已注册的整个期间保持有效，且注册期间不得直接修改其字段。
  */
typedef struct GPIOEXTI_STM32HALAdapter_Callback
{
    GPIOEXTI_STM32HALAdapter_HandlerTypeDef Handler; /**< ISR 中调用的处理函数。 */
    void *Context;                                  /**< 原样传递给 Handler 的调用者对象。 */
    uint16_t PinMask;                               /**< 订阅的 GPIO_Pin 位掩码。 */
    struct GPIOEXTI_STM32HALAdapter_Callback *Next; /**< Adapter 私有链表中的下一节点。 */
} GPIOEXTI_STM32HALAdapter_CallbackTypeDef;

GPIOEXTI_STM32HALAdapter_StatusTypeDef GPIOEXTI_STM32HALAdapter_Register(
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *callback,
    uint16_t pin_mask,
    GPIOEXTI_STM32HALAdapter_HandlerTypeDef handler,
    void *context);

GPIOEXTI_STM32HALAdapter_StatusTypeDef GPIOEXTI_STM32HALAdapter_Unregister(
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *callback);

#endif /* GPIO_EXTI_STM32_HAL_ADAPTER_H */
