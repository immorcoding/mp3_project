#include "Platform/platform_irq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "Adapters/irq/irq_stm32_hal_adapter.h"
#include "main.h"

typedef struct
{
    Platform_IRQ_CallbackTypeDef volatile Callback;
    void * volatile Context;
} Platform_IRQ_SlotTypeDef;

typedef enum
{
    PLATFORM_IRQ_STATE_RESET = 0,
    PLATFORM_IRQ_STATE_READY
} Platform_IRQ_StateTypeDef;

typedef struct
{
    Platform_IRQ_SlotTypeDef Slots[PLATFORM_IRQ_SOURCE_COUNT];
    volatile Platform_IRQ_StateTypeDef State;
} Platform_IRQ_HandleTypeDef;

static Platform_IRQ_HandleTypeDef hplatform_irq;
static IRQ_STM32HALAdapterTypeDef hplatform_irq_adapter;

/**
  * @brief  判断逻辑中断源是否可以安全索引回调槽数组。
  */
static bool platform_irq_source_is_valid(Platform_IRQ_SourceTypeDef source)
{
    return (uint32_t)source < (uint32_t)PLATFORM_IRQ_SOURCE_COUNT;
}

/**
  * @brief  在 ISR 中把逻辑中断源分发给已注册的 Platform Module 回调。
  */
static void platform_irq_dispatch_from_isr(Platform_IRQ_SourceTypeDef source)
{
    Platform_IRQ_CallbackTypeDef callback;
    void *context;

    if ((hplatform_irq.State != PLATFORM_IRQ_STATE_READY) ||
        !platform_irq_source_is_valid(source))
    {
        return;
    }

    callback = hplatform_irq.Slots[(uint32_t)source].Callback;

    if (callback == NULL)
    {
        return;
    }

    context = hplatform_irq.Slots[(uint32_t)source].Context;
    callback(context);
}

/**
  * @brief  把 STM32 HAL Adapter 上报的物理 GPIO 引脚转换为板级逻辑中断源。
  * @param  gpio_pin STM32 HAL 传入的 GPIO 引脚位掩码。
  * @param  context 当前实现不需要对象上下文。
  * @note   本函数运行在 ISR 上下文，只负责映射和分发，不执行设备访问、
  *         消抖、日志输出或文件系统操作。
  */
static void platform_irq_stm32_hal_callback(uint16_t gpio_pin, void *context)
{
    (void)context;

    if (gpio_pin == SD_CD_Pin)
    {
        platform_irq_dispatch_from_isr(PLATFORM_IRQ_SOURCE_SD_DETECT);
    }
    else if (gpio_pin == AXP2101_IRQ_Pin)
    {
        platform_irq_dispatch_from_isr(PLATFORM_IRQ_SOURCE_PMIC);
    }
}

/**
  * @brief  初始化板级 IRQ Dispatcher 并绑定 STM32 HAL EXTI Adapter。
  * @retval PLATFORM_OK 初始化和 Adapter 绑定成功。
  * @retval PLATFORM_IRQ_ERROR Adapter 已被其他实例占用。
  * @note   必须先于任何 Platform Module 注册中断回调。
  */
Platform_StatusTypeDef Platform_IRQ_Init(void)
{
    uint32_t index;

    hplatform_irq.State = PLATFORM_IRQ_STATE_RESET;

    for (index = 0U; index < (uint32_t)PLATFORM_IRQ_SOURCE_COUNT; index++)
    {
        hplatform_irq.Slots[index].Callback = NULL;
        hplatform_irq.Slots[index].Context = NULL;
    }

    if (IRQ_STM32HALAdapter_Bind(&hplatform_irq_adapter,
                                 platform_irq_stm32_hal_callback,
                                 NULL) != IRQ_STM32_HAL_ADAPTER_OK)
    {
        return PLATFORM_IRQ_ERROR;
    }

    hplatform_irq.State = PLATFORM_IRQ_STATE_READY;
    return PLATFORM_OK;
}

/**
  * @brief  为一个板级逻辑中断源注册唯一的回调和对象上下文。
  */
Platform_StatusTypeDef Platform_IRQ_Register(
    Platform_IRQ_SourceTypeDef source,
    Platform_IRQ_CallbackTypeDef callback,
    void *context)
{
    Platform_IRQ_SlotTypeDef *slot;
    uint32_t primask;

    if ((hplatform_irq.State != PLATFORM_IRQ_STATE_READY) ||
        !platform_irq_source_is_valid(source) ||
        (callback == NULL))
    {
        return PLATFORM_IRQ_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    slot = &hplatform_irq.Slots[(uint32_t)source];

    if (slot->Callback != NULL)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }

        return PLATFORM_IRQ_ERROR;
    }

    /*
     * Context 先写、Callback 后写；Callback 同时作为该槽已经发布的标志。
     */
    slot->Context = context;
    slot->Callback = callback;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return PLATFORM_OK;
}

/**
  * @brief  注销一个板级逻辑中断源当前绑定的回调。
  */
Platform_StatusTypeDef Platform_IRQ_Unregister(
    Platform_IRQ_SourceTypeDef source)
{
    Platform_IRQ_SlotTypeDef *slot;
    uint32_t primask;

    if ((hplatform_irq.State != PLATFORM_IRQ_STATE_READY) ||
        !platform_irq_source_is_valid(source))
    {
        return PLATFORM_IRQ_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    slot = &hplatform_irq.Slots[(uint32_t)source];

    if (slot->Callback == NULL)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }

        return PLATFORM_IRQ_ERROR;
    }

    /*
     * 先撤销 Callback，再清除 Context，阻止后续 ISR 使用已经注销的对象。
     */
    slot->Callback = NULL;
    slot->Context = NULL;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return PLATFORM_OK;
}
