/**
  ******************************************************************************
  * @file    stm32_gpio_exti_irq.c
  * @brief   STM32 HAL GPIO EXTI 入口和调用者持有回调链表的 Adapter。
  *
  * @details
  *          STM32 HAL 把所有 GPIO EXTI 事件汇聚到唯一的
  *          HAL_GPIO_EXTI_Callback()。本文件拥有该入口，并把已经由 HAL
  *          确认的引脚事件分发给长期注册的调用者对象。
  *
  *          链表只允许在普通执行上下文通过 Register()/Unregister() 修改；
  *          ISR 中只遍历和调用匹配 Handler。因此调用者的 Handler 不能注销
  *          自己、不能修改 Callback 字段，也不能做日志、阻塞或外设访问。
  ******************************************************************************
  */

#include "Adapters/irq/stm32_gpio_exti_irq.h"

#include <stddef.h>

#include "stm32h7xx_hal.h"

/** @brief 已注册 GPIO EXTI 回调对象的侵入式单向链表表头。 */
static GPIOEXTI_STM32HALAdapter_CallbackTypeDef * volatile hgpio_exti_callback_list;

/**
  * @brief  为一组 GPIO EXTI 引脚注册调用者持有的回调对象。
  * @param  callback 调用者长期持有的回调对象；成功后直至注销前不得释放。
  * @param  pin_mask 需要接收的 HAL GPIO_Pin 位掩码，可同时包含多个引脚。
  * @param  handler 匹配引脚到达时在 ISR 上下文调用的处理函数。
  * @param  context 调用 handler 时原样传回的调用者上下文，可为 NULL。
  * @retval GPIOEXTI_STM32HAL_ADAPTER_OK 注册成功。
  * @retval GPIOEXTI_STM32HAL_ADAPTER_ERROR 参数无效或对象已经注册。
  * @note   本函数只能在普通执行上下文调用，不能从 ISR 或已注册 Handler 中
  *         调用。函数在短临界区内完整填写对象后才把它发布为链表表头。
  */
GPIOEXTI_STM32HALAdapter_StatusTypeDef GPIOEXTI_STM32HALAdapter_Register(
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *callback,
    uint16_t pin_mask,
    GPIOEXTI_STM32HALAdapter_HandlerTypeDef handler,
    void *context)
{
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *cursor;
    uint32_t primask;

    if ((callback == NULL) || (pin_mask == 0U) || (handler == NULL))
    {
        return GPIOEXTI_STM32HAL_ADAPTER_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    cursor = hgpio_exti_callback_list;

    while (cursor != NULL)
    {
        if (cursor == callback)
        {
            if (primask == 0U)
            {
                __enable_irq();
            }

            return GPIOEXTI_STM32HAL_ADAPTER_ERROR;
        }

        cursor = cursor->Next;
    }

    callback->Handler = handler;
    callback->Context = context;
    callback->PinMask = pin_mask;
    callback->Next = hgpio_exti_callback_list;
    hgpio_exti_callback_list = callback;

    if (primask == 0U) // 仅在调用前中断未被禁止时恢复中断状态
    {
        __enable_irq();
    }

    return GPIOEXTI_STM32HAL_ADAPTER_OK;
}

/**
  * @brief  从 GPIO EXTI 回调链表注销一个回调对象。
  * @param  callback 当前已经注册的调用者持有对象。
  * @retval GPIOEXTI_STM32HAL_ADAPTER_OK 注销成功。
  * @retval GPIOEXTI_STM32HAL_ADAPTER_ERROR 参数无效或对象当前未注册。
  * @note   本函数只能在普通执行上下文调用。成功返回后，ISR 已不可能再访问
  *         callback，调用者可以安全地复用或销毁该对象。
  */
GPIOEXTI_STM32HALAdapter_StatusTypeDef GPIOEXTI_STM32HALAdapter_Unregister(
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *callback)
{
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *cursor;
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *previous = NULL;
    uint32_t primask;

    if (callback == NULL)
    {
        return GPIOEXTI_STM32HAL_ADAPTER_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    cursor = hgpio_exti_callback_list;

    while ((cursor != NULL) && (cursor != callback))
    {
        previous = cursor;
        cursor = cursor->Next;
    }

    if (cursor == NULL)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }

        return GPIOEXTI_STM32HAL_ADAPTER_ERROR;
    }

    if (previous == NULL)
    {
        hgpio_exti_callback_list = callback->Next;
    }
    else
    {
        previous->Next = callback->Next;
    }

    callback->Handler = NULL;
    callback->Context = NULL;
    callback->PinMask = 0U;
    callback->Next = NULL;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return GPIOEXTI_STM32HAL_ADAPTER_OK;
}

/**
  * @brief  把 STM32 HAL GPIO EXTI 事件分发给全部匹配的已注册回调。
  * @param  GPIO_Pin HAL 已确认并清除挂起状态的 GPIO 引脚位掩码。
  * @note   本函数在 ISR 上下文执行。它只执行 PinMask 匹配和 Handler 调用，
  *         不解释 SD、PMIC、按键等产品语义。
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    GPIOEXTI_STM32HALAdapter_CallbackTypeDef *callback = hgpio_exti_callback_list;

    while (callback != NULL)
    {
        if ((callback->Handler != NULL) &&
            ((callback->PinMask & GPIO_Pin) != 0U))
        {
            callback->Handler(GPIO_Pin, callback->Context);
        }

        callback = callback->Next;
    }
}
