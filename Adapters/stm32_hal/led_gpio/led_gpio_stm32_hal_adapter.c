/**
  ******************************************************************************
  * @file    led_gpio_stm32_hal_adapter.c
  * @brief   STM32 HAL GPIO 到 LED Device Interface 的 Adapter。
  *
  * @details
  *          Adapter 只负责验证已由 CubeMX 配置的 GPIO Context，并把 Device 的逻辑
  *          ON/OFF 映射为当前板级所需的物理电平。它不拥有闪烁周期、LED 编号、
  *          FreeRTOS 或任何上层产品状态。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/led_gpio/led_gpio_stm32_hal_adapter.h"

#include <stddef.h>

/**
  * @brief  验证 GPIO LED Adapter Context 可用于后续写引脚操作。
  * @param  context 指向 LED_GPIO_STM32HALAdapterTypeDef 的有效地址。
  * @retval LED_PORT_OK Context 完整，且逻辑 ON 电平合法。
  * @retval LED_PORT_ERROR Context 或板级 GPIO 定义无效。
  * @note   GPIO 的模式、速度与初始输出值仍完全由 CubeMX 的 MX_GPIO_Init() 拥有；
  *         本函数不重复配置引脚，避免 Adapter 覆盖板级生成配置。
  */
static LED_PortStatusTypeDef led_gpio_stm32_hal_initialize(void *context)
{
    const LED_GPIO_STM32HALAdapterTypeDef *adapter =
        (const LED_GPIO_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) ||
        (adapter->Port == NULL) ||
        (adapter->Pin == 0U) ||
        ((adapter->OnState != GPIO_PIN_RESET) && (adapter->OnState != GPIO_PIN_SET)))
    {
        return LED_PORT_ERROR;
    }

    return LED_PORT_OK;
}

/**
  * @brief  将 LED Device 的逻辑亮灭状态写入 STM32 GPIO。
  * @param  context 指向 LED_GPIO_STM32HALAdapterTypeDef 的有效地址。
  * @param  state Device 请求的逻辑 ON/OFF 状态。
  * @retval LED_PORT_OK GPIO 输出锁存值已写入。
  * @retval LED_PORT_ERROR Context 或逻辑状态无效。
  * @note   HAL_GPIO_WritePin() 没有失败返回值。对于 GPIO LED，写入请求即视为已被
  *         后端接受；无法诊断引脚外部短路、断线或 LED 本体故障。
  */
static LED_PortStatusTypeDef led_gpio_stm32_hal_set_state(
    void *context,
    LED_OnOffTypeDef state)
{
    const LED_GPIO_STM32HALAdapterTypeDef *adapter =
        (const LED_GPIO_STM32HALAdapterTypeDef *)context;
    GPIO_PinState output_state;

    if ((adapter == NULL) ||
        (adapter->Port == NULL) ||
        (adapter->Pin == 0U) ||
        ((adapter->OnState != GPIO_PIN_RESET) && (adapter->OnState != GPIO_PIN_SET)) ||
        ((state != LED_OFF) && (state != LED_ON)))
    {
        return LED_PORT_ERROR;
    }

    if (state == LED_ON)
    {
        output_state = adapter->OnState;
    }
    else
    {
        output_state = (adapter->OnState == GPIO_PIN_SET) ?
                           GPIO_PIN_RESET :
                           GPIO_PIN_SET;
    }

    HAL_GPIO_WritePin(adapter->Port, adapter->Pin, output_state);
    return LED_PORT_OK;
}

/** @brief LED Device 使用的 STM32 HAL GPIO Port 操作表。 */
static const LED_PortOpsTypeDef led_gpio_stm32_hal_port_ops = {
    .Initialize = led_gpio_stm32_hal_initialize,
    .SetState = led_gpio_stm32_hal_set_state
};

/**
  * @brief  将 STM32 HAL GPIO LED Adapter 安装到 LED Device Handle。
  * @param  hled 待绑定的 LED Device Handle。
  * @param  adapter Platform 长期持有的 GPIO LED Adapter Context。
  * @retval LED_OK Interface 与 Context 已成对安装。
  * @retval LED_ERROR Handle、Context、GPIO 引用或 ON 电平定义无效。
  * @note   本函数只完成依赖装配，不初始化 GPIO、不改变 LED 输出电平。调用者随后
  *         必须执行 LED_Init()，由 Device 统一发出初始 OFF 请求。
  */
LED_StatusTypeDef LED_GPIO_STM32HALAdapter_Bind(
    LED_HandleTypeDef *hled,
    LED_GPIO_STM32HALAdapterTypeDef *adapter)
{
    if ((hled == NULL) ||
        (adapter == NULL) ||
        (adapter->Port == NULL) ||
        (adapter->Pin == 0U) ||
        ((adapter->OnState != GPIO_PIN_RESET) && (adapter->OnState != GPIO_PIN_SET)))
    {
        return LED_ERROR;
    }

    hled->PortOps = &led_gpio_stm32_hal_port_ops;
    hled->PortContext = adapter;
    return LED_OK;
}
