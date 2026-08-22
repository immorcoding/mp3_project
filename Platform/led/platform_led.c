/**
  ******************************************************************************
  * @file    platform_led.c
  * @brief   当前 PCB LED Device 与 GPIO Adapter 的 Platform 装配实现。
  *
  * @details
  *          本 Module 长期持有每个 LED Device 与具体 Adapter Context，并按稳定的
  *          Platform 编号把上层逻辑 ON/OFF 请求转交给 LED Component。当前 STATUS
  *          LED 使用 GPIO；未来 LED 后端可以是 I2C、PWM 或扩展 IO，而不改变本
  *          Module 的公开 Interface。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Platform/led/platform_led.h"

#include <stdbool.h>

#include "Adapters/stm32_hal/led_gpio/led_gpio_stm32_hal_adapter.h"
#include "Components/led/led.h"
#include "Components/log/log.h"
#include "main.h"

/** @brief 当前 PCB 中每一个已公开 LED 的 Device Handle。 */
static LED_HandleTypeDef hplatform_leds[PLATFORM_LED_ID_COUNT];

/**
  * @brief 当前 PCB LED 编号对应的具体 STM32 HAL GPIO Adapter Context。
  * @note  USER_LED 的 CubeMX 默认初始电平为 RESET，故本板把逻辑 ON 映射为 SET；
  *        若后续原理图改为低有效，只修改 OnState，不影响 Platform 或上层调用者。
  */
static LED_GPIO_STM32HALAdapterTypeDef hplatform_led_gpio_adapters[PLATFORM_LED_ID_COUNT] = {
    [PLATFORM_LED_ID_STATUS] = {
        .Port = USER_LED_GPIO_Port,
        .Pin = USER_LED_Pin,
        .OnState = GPIO_PIN_SET
    }
};

/**
  * @brief  检查调用者给出的 Platform LED 编号是否属于当前装配表。
  * @param  id 待检查的逻辑 LED 编号。
  * @retval true 编号有效。
  * @retval false 编号无效或为哨兵值。
  */
static bool platform_led_is_valid_id(Platform_LED_IdTypeDef id)
{
    return ((unsigned int)id < (unsigned int)PLATFORM_LED_ID_COUNT);
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  绑定并初始化当前 PCB 中所有已公开 LED。
  * @retval PLATFORM_OK 所有 LED 均已进入逻辑关闭的 READY 状态。
  * @retval PLATFORM_LED_ERROR 至少一盏 LED 的 Adapter 绑定或初始化失败。
  * @note   此错误仅降级诊断能力，不应阻止播放器启动。函数会逐一尝试全部 LED，
  *         以便多个独立后端的故障不会互相掩盖；详细原因同时写入启动日志。
  */
Platform_StatusTypeDef Platform_LED_Init(void)
{
    Platform_StatusTypeDef result = PLATFORM_OK;

    for (unsigned int index = 0U;
         index < (unsigned int)PLATFORM_LED_ID_COUNT;
         ++index)
    {
        if (LED_GPIO_STM32HALAdapter_Bind(&hplatform_leds[index],
                                           &hplatform_led_gpio_adapters[index]) != LED_OK)
        {
            (void)LOG_Printf(LOG_LEVEL_ERROR,
                             "LED",
                             "Adapter bind failed: id=%u.",
                             index);
            result = PLATFORM_LED_ERROR;
            continue;
        }

        if (LED_Init(&hplatform_leds[index]) != LED_OK)
        {
            (void)LOG_Printf(LOG_LEVEL_ERROR,
                             "LED",
                             "Initialization failed: id=%u, error=%u, port=%u.",
                             index,
                             (unsigned int)hplatform_leds[index].ErrorCode,
                             (unsigned int)hplatform_leds[index].LastPortStatus);
            result = PLATFORM_LED_ERROR;
        }
    }

    return result;
}

/**
  * @brief  设置指定板级 LED 的逻辑 ON/OFF 状态。
  * @param  id 当前 PCB 中的逻辑 LED 编号。
  * @param  state 请求的逻辑亮灭状态，不表示 GPIO 电平。
  * @retval PLATFORM_OK LED Device 已成功把请求交给具体后端。
  * @retval PLATFORM_LED_ERROR 编号、状态或 Device 当前状态无效，或底层后端失败。
  * @note   本函数不记录周期性控制失败日志，避免故障 LED 被 Monitor Task 高频调用时
  *         挤占日志队列；需要诊断时可由调用者按其业务频率选择记录。
  */
Platform_StatusTypeDef Platform_LED_Set(Platform_LED_IdTypeDef id,
                                        Platform_LED_OnOffTypeDef state)
{
    LED_OnOffTypeDef led_state;

    if ((!platform_led_is_valid_id(id)) ||
        ((state != PLATFORM_LED_OFF) && (state != PLATFORM_LED_ON)))
    {
        return PLATFORM_LED_ERROR;
    }

    led_state = (state == PLATFORM_LED_ON) ? LED_ON : LED_OFF;
    return (LED_Set(&hplatform_leds[(unsigned int)id], led_state) == LED_OK) ?
               PLATFORM_OK :
               PLATFORM_LED_ERROR;
}

/**
  * @brief  翻转指定板级 LED 的逻辑亮灭状态。
  * @param  id 当前 PCB 中的逻辑 LED 编号。
  * @retval PLATFORM_OK LED 已成功翻转。
  * @retval PLATFORM_LED_ERROR 编号无效、LED 未就绪或底层后端失败。
  * @note   翻转依据 LED Device 最后一次成功提交的逻辑状态，不读取物理 GPIO 电平。
  */
Platform_StatusTypeDef Platform_LED_Toggle(Platform_LED_IdTypeDef id)
{
    if (!platform_led_is_valid_id(id))
    {
        return PLATFORM_LED_ERROR;
    }

    return (LED_Toggle(&hplatform_leds[(unsigned int)id]) == LED_OK) ?
               PLATFORM_OK :
               PLATFORM_LED_ERROR;
}
