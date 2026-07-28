#include "Adapters/irq/irq_stm32_hal_adapter.h"

#include <stddef.h>

#include "stm32h7xx_hal.h"

/*
 * STM32 HAL 只提供一个全局 HAL_GPIO_EXTI_Callback() 入口，因此同一时刻
 * 只能有一个 Platform IRQ Dispatcher 绑定到该入口。
 */
static IRQ_STM32HALAdapterTypeDef * volatile hactive_irq_adapter;

/**
  * @brief  将 Platform IRQ Dispatcher 绑定到 STM32 HAL EXTI 回调入口。
  * @param  adapter 由 Platform 长期持有的 Adapter 实例。
  * @param  callback 接收原始 GPIO Pin 的 ISR 回调。
  * @param  context 调用 callback 时传回的对象上下文。
  * @retval IRQ_STM32_HAL_ADAPTER_OK 绑定成功。
  * @retval IRQ_STM32_HAL_ADAPTER_ERROR 参数无效或已有其他实例占用全局入口。
  * @note   本函数只能在普通执行上下文调用。
  */
IRQ_STM32HALAdapter_StatusTypeDef IRQ_STM32HALAdapter_Bind(
    IRQ_STM32HALAdapterTypeDef *adapter,
    IRQ_STM32HALAdapter_CallbackTypeDef callback,
    void *context)
{
    uint32_t primask;

    if ((adapter == NULL) || (callback == NULL))
    {
        return IRQ_STM32_HAL_ADAPTER_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if ((hactive_irq_adapter != NULL) && (hactive_irq_adapter != adapter))
    {
        if (primask == 0U)
        {
            __enable_irq();
        }

        return IRQ_STM32_HAL_ADAPTER_ERROR;
    }

    /*
     * 先发布 Context 和 Callback，最后发布全局实例指针。ISR 只要能取得
     * hactive_irq_adapter，就一定能看到一组完整绑定。
     */
    adapter->Context = context;
    adapter->Callback = callback;
    hactive_irq_adapter = adapter;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return IRQ_STM32_HAL_ADAPTER_OK;
}

/**
  * @brief  解除当前 Platform IRQ Dispatcher 与 STM32 HAL EXTI 的绑定。
  * @param  adapter 当前已绑定的 Adapter 实例。
  * @retval IRQ_STM32_HAL_ADAPTER_OK 解绑成功。
  * @retval IRQ_STM32_HAL_ADAPTER_ERROR 参数无效或该实例当前未绑定。
  * @note   本函数只能在普通执行上下文调用。
  */
IRQ_STM32HALAdapter_StatusTypeDef IRQ_STM32HALAdapter_Unbind(
    IRQ_STM32HALAdapterTypeDef *adapter)
{
    uint32_t primask;

    if (adapter == NULL)
    {
        return IRQ_STM32_HAL_ADAPTER_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if (hactive_irq_adapter != adapter)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }

        return IRQ_STM32_HAL_ADAPTER_ERROR;
    }

    /*
     * 先撤销全局可见性，再清空实例内容。解绑完成后到达的 EXTI 不会再
     * 进入 Platform；解绑前已经开始执行的回调允许自然完成。
     */
    hactive_irq_adapter = NULL;
    adapter->Callback = NULL;
    adapter->Context = NULL;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return IRQ_STM32_HAL_ADAPTER_OK;
}

/**
  * @brief  把 STM32 HAL GPIO EXTI 回调转交给当前绑定的 Platform Dispatcher。
  * @param  GPIO_Pin HAL 已确认并清除挂起状态的 GPIO 引脚位掩码。
  * @note   回调仍运行在 ISR 上下文；Adapter 不解释引脚的产品含义。
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    IRQ_STM32HALAdapterTypeDef *adapter = hactive_irq_adapter;
    IRQ_STM32HALAdapter_CallbackTypeDef callback;
    void *context;

    if (adapter == NULL)
    {
        return;
    }

    callback = adapter->Callback;
    context = adapter->Context;

    if (callback != NULL)
    {
        callback(GPIO_Pin, context);
    }
}
