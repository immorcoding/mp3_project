/**
  ******************************************************************************
  * @file    irq_stm32_hal_adapter.c
  * @brief   STM32 HAL GPIO EXTI 入口和调用者持有回调链表的 Adapter。
  *
  * @details
  *          Adapter 独占 HAL_GPIO_EXTI_Callback()，按 GPIO PinMask 向已注册
  *          回调分发事件，但不解释 SD、PMIC 等产品语义。
  ******************************************************************************
  */

#include "Adapters/irq/irq_stm32_hal_adapter.h"

#include <stddef.h>

#include "stm32h7xx_hal.h"

/*
 * STM32 HAL 只提供一个全局 HAL_GPIO_EXTI_Callback() 入口。Adapter 独占该
 * 入口，并维护由各使用者持有的回调对象链表；链表只在普通上下文中修改。
 */
static IRQ_STM32HALAdapter_CallbackTypeDef * volatile hirq_callback_list;

/**
  * @brief  为一组 GPIO EXTI 引脚注册调用者持有的回调对象。
  * @param  callback 调用者长期持有的回调对象，注册期间不得释放或修改。
  * @param  pin_mask 需要接收的 GPIO_Pin 位掩码，可以同时包含多个引脚。
  * @param  handler 匹配引脚到达时在 ISR 上下文调用的处理函数。
  * @param  context 调用 handler 时原样传回的对象上下文，可以为 NULL。
  * @retval IRQ_STM32_HAL_ADAPTER_OK 注册成功。
  * @retval IRQ_STM32_HAL_ADAPTER_ERROR 参数无效或对象已经注册。
  * @note   本函数只能在普通执行上下文调用，不能从 ISR 或回调中调用。
  */
IRQ_STM32HALAdapter_StatusTypeDef IRQ_STM32HALAdapter_Register(
    IRQ_STM32HALAdapter_CallbackTypeDef *callback,
    uint16_t pin_mask,
    IRQ_STM32HALAdapter_HandlerTypeDef handler,
    void *context)
{
    IRQ_STM32HALAdapter_CallbackTypeDef *cursor;
    uint32_t primask;

    if ((callback == NULL) || (pin_mask == 0U) || (handler == NULL))
    {
        return IRQ_STM32_HAL_ADAPTER_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    /*
     * 不依赖调用者预先清零回调对象，而是按对象地址检查重复注册。
     * 这样静态对象、栈外长期对象和重新使用的对象遵循相同规则。
     */
    cursor = hirq_callback_list;

    while (cursor != NULL)
    {
        if (cursor == callback)
        {
            if (primask == 0U)
            {
                __enable_irq();
            }

            return IRQ_STM32_HAL_ADAPTER_ERROR;
        }

        cursor = cursor->Next;
    }

    /*
     * 先完整填写回调对象，再把它发布为链表头。中断保持关闭，因此 ISR
     * 不会观察到尚未完整初始化的对象。
     */
    callback->Handler = handler;
    callback->Context = context;
    callback->PinMask = pin_mask;
    callback->Next = hirq_callback_list;
    hirq_callback_list = callback;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return IRQ_STM32_HAL_ADAPTER_OK;
}

/**
  * @brief  从 GPIO EXTI 回调链表注销一个回调对象。
  * @param  callback 当前已经注册的回调对象。
  * @retval IRQ_STM32_HAL_ADAPTER_OK 注销成功。
  * @retval IRQ_STM32_HAL_ADAPTER_ERROR 参数无效或对象当前未注册。
  * @note   本函数只能在普通执行上下文调用，返回后对象即可安全释放或复用。
  */
IRQ_STM32HALAdapter_StatusTypeDef IRQ_STM32HALAdapter_Unregister(
    IRQ_STM32HALAdapter_CallbackTypeDef *callback)
{
    IRQ_STM32HALAdapter_CallbackTypeDef *cursor;
    IRQ_STM32HALAdapter_CallbackTypeDef *previous = NULL;
    uint32_t primask;

    if (callback == NULL)
    {
        return IRQ_STM32_HAL_ADAPTER_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    cursor = hirq_callback_list;

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

        return IRQ_STM32_HAL_ADAPTER_ERROR;
    }

    if (previous == NULL)
    {
        hirq_callback_list = callback->Next;
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

    return IRQ_STM32_HAL_ADAPTER_OK;
}

/**
  * @brief  把 STM32 HAL GPIO EXTI 事件分发给所有匹配的已注册回调。
  * @param  GPIO_Pin HAL 已确认并清除挂起状态的 GPIO 引脚位掩码。
  * @note   本函数运行在 ISR 上下文。Adapter 只按 PinMask 匹配，不解释
  *         引脚对应 SD、PMIC 或其他产品含义。
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    IRQ_STM32HALAdapter_CallbackTypeDef *callback = hirq_callback_list;

    while (callback != NULL)
    {
        /*
         * Register/Unregister 不允许在 ISR 中调用，因此遍历期间链表不会
         * 被同一内核上的普通上下文并发改写。
         */
        if ((callback->Handler != NULL) &&
            ((callback->PinMask & GPIO_Pin) != 0U))
        {
            callback->Handler(GPIO_Pin, callback->Context);
        }

        callback = callback->Next;
    }
}
