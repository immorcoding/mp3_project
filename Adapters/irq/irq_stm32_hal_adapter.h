/**
  ******************************************************************************
  * @file    irq_stm32_hal_adapter.h
  * @brief   STM32 HAL GPIO EXTI 回调对象和注册接口。
  ******************************************************************************
  */

#ifndef IRQ_STM32_HAL_ADAPTER_H
#define IRQ_STM32_HAL_ADAPTER_H

#include <stdint.h>

/**
  * @brief STM32 HAL IRQ Adapter 操作结果。
  */
typedef enum
{
    IRQ_STM32_HAL_ADAPTER_OK = 0, /**< 回调注册或注销成功。 */
    IRQ_STM32_HAL_ADAPTER_ERROR  /**< 参数非法、重复注册或目标未注册。 */
} IRQ_STM32HALAdapter_StatusTypeDef;

/**
  * @brief 接收 STM32 HAL 原始 GPIO EXTI 引脚的中断处理函数。
  * @note  回调运行在 ISR 上下文。
  */
typedef void (*IRQ_STM32HALAdapter_HandlerTypeDef)(uint16_t gpio_pin,
                                                    void *context);

/**
  * @brief 调用者持有的 GPIO EXTI 回调对象。
  * @note  本结构采用与 Zephyr gpio_callback 相似的侵入式链表设计。
  *        注册期间不得修改任何字段；只能通过 Register/Unregister 改变生命周期。
  */
typedef struct IRQ_STM32HALAdapter_Callback
{
    IRQ_STM32HALAdapter_HandlerTypeDef Handler; /**< ISR 中调用的处理函数。 */
    void *Context;                              /**< 原样传递给 Handler 的调用者对象。 */
    uint16_t PinMask;                           /**< 订阅的 GPIO_Pin 位掩码。 */
    struct IRQ_STM32HALAdapter_Callback *Next;  /**< Adapter 私有链表中的下一节点。 */
} IRQ_STM32HALAdapter_CallbackTypeDef;

IRQ_STM32HALAdapter_StatusTypeDef IRQ_STM32HALAdapter_Register(
    IRQ_STM32HALAdapter_CallbackTypeDef *callback,
    uint16_t pin_mask,
    IRQ_STM32HALAdapter_HandlerTypeDef handler,
    void *context);

IRQ_STM32HALAdapter_StatusTypeDef IRQ_STM32HALAdapter_Unregister(
    IRQ_STM32HALAdapter_CallbackTypeDef *callback);

#endif /* IRQ_STM32_HAL_ADAPTER_H */
