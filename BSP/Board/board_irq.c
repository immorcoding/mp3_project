#include "BSP/Board/board_irq.h"

#include <stddef.h>
#include <stdint.h>

#include "main.h"

/**
  * @brief 一个逻辑中断源对应的回调绑定槽。
  * @note  Cb 与 Context 共同表示“行为 + 对象实例”，相当于 C++ 对象的成员函数绑定。
  */
typedef struct
{
    Board_IRQ_CallbackTypeDef volatile Cb;
    void * volatile Context;
} Board_IRQ_SlotTypeDef;

/**
  * @brief Board IRQ Dispatcher 的内部生命周期状态。
  */
typedef enum
{
    BOARD_IRQ_STATE_RESET = 0,
    BOARD_IRQ_STATE_READY
} Board_IRQ_StateTypeDef;

/**
  * @brief Board IRQ Dispatcher 的私有 Handle。
  * @note  Slots 保存所有运行期绑定；State 防止初始化完成前访问未准备好的表。
  */
typedef struct
{
    Board_IRQ_SlotTypeDef Slots[BOARD_IRQ_SOURCE_COUNT];
    volatile Board_IRQ_StateTypeDef State;
} Board_IRQ_HandleTypeDef;

/** @brief 本板唯一的 IRQ Dispatcher 实例，不向 APP 暴露。 */
static Board_IRQ_HandleTypeDef hboard_irq;

/**
  * @brief  判断逻辑中断源是否可用于索引 Slots 数组。
  * @param  source 待检查的逻辑中断源。
  * @retval 1 参数有效。
  * @retval 0 参数无效。
  * @note   转成 uint32_t 后，负枚举值也会变为大数并被范围检查拒绝。
  */
static uint32_t board_irq_source_is_valid(Board_IRQ_SourceTypeDef source)
{
    return ((uint32_t)source < (uint32_t)BOARD_IRQ_SOURCE_COUNT) ? 1U : 0U;
}

/**
  * @brief  初始化 IRQ Dispatcher 并清除全部历史绑定。
  * @retval BOARD_OK 初始化完成。
  * @note   应在任何 Board Module 注册中断回调之前调用。
  */
Board_StatusTypeDef Board_IRQ_Init(void)
{
    uint32_t index;

    /*
     * 先进入 RESET，确保重新初始化期间到达的中断不会读取一张正在清理的表。
     * 当前工程在启动阶段调用本函数；该顺序也使以后支持显式重新初始化时更安全。
     */
    hboard_irq.State = BOARD_IRQ_STATE_RESET;

    for (index = 0U; index < (uint32_t)BOARD_IRQ_SOURCE_COUNT; index++)
    {
        hboard_irq.Slots[index].Cb = NULL;
        hboard_irq.Slots[index].Context = NULL;
    }

    hboard_irq.State = BOARD_IRQ_STATE_READY;

    return BOARD_OK;
}

/**
  * @brief  为一个逻辑中断源绑定回调和对象上下文。
  */
Board_StatusTypeDef Board_IRQ_Register(Board_IRQ_SourceTypeDef source,
                                       Board_IRQ_CallbackTypeDef cb,
                                       void *context)
{
    Board_IRQ_SlotTypeDef *slot;

    if ((hboard_irq.State != BOARD_IRQ_STATE_READY) ||
        (board_irq_source_is_valid(source) == 0U) ||
        (cb == NULL))
    {
        return BOARD_IRQ_ERROR;
    }

    slot = &hboard_irq.Slots[(uint32_t)source];

    /* 不静默覆盖已有对象；调用者必须先显式 Unregister。 */
    if (slot->Cb != NULL)
    {
        return BOARD_IRQ_ERROR;
    }

    /*
     * Context 先写、Cb 后写。Cb 是这个槽的“有效标志”，因此 ISR 只可能看到：
     * 1. Cb == NULL：槽尚未发布，直接忽略；
     * 2. Cb != NULL：对应 Context 已经写入。
     */
    slot->Context = context;
    slot->Cb = cb;

    return BOARD_OK;
}

/**
  * @brief  解除一个逻辑中断源的当前绑定。
  */
Board_StatusTypeDef Board_IRQ_Unregister(Board_IRQ_SourceTypeDef source)
{
    Board_IRQ_SlotTypeDef *slot;

    if ((hboard_irq.State != BOARD_IRQ_STATE_READY) ||
        (board_irq_source_is_valid(source) == 0U))
    {
        return BOARD_IRQ_ERROR;
    }

    slot = &hboard_irq.Slots[(uint32_t)source];

    if (slot->Cb == NULL)
    {
        return BOARD_IRQ_ERROR;
    }

    /*
     * Cb 先清空，使之后到达的 ISR 不再调用旧对象；Context 随后清空。
     * 如果中断发生在函数开始、Cb 清空之前，旧回调仍可能完成一次调用，
     * 这属于“注销完成前已经到达的事件”，调用者不能提前销毁 Context。
     */
    slot->Cb = NULL;
    slot->Context = NULL;

    return BOARD_OK;
}

/**
  * @brief  在 ISR 中查找并调用指定逻辑中断源的回调。
  */
void Board_IRQ_DispatchFromISR(Board_IRQ_SourceTypeDef source)
{
    Board_IRQ_CallbackTypeDef cb;
    void *context;

    if ((hboard_irq.State != BOARD_IRQ_STATE_READY) ||
        (board_irq_source_is_valid(source) == 0U))
    {
        return;
    }

    /*
     * 先复制 Cb，再读取 Context。局部副本保证本次分发始终调用同一个函数，
     * 也避免回调执行期间重复访问共享槽。
     */
    cb = hboard_irq.Slots[(uint32_t)source].Cb;

    if (cb == NULL)
    {
        return;
    }

    context = hboard_irq.Slots[(uint32_t)source].Context;
    cb(context, source);
}

/**
  * @brief  将 STM32 HAL GPIO EXTI 回调转换为板级逻辑中断源。
  * @param  GPIO_Pin HAL 传入的已确认挂起的 GPIO 引脚位掩码。
  * @note   EXTI9_5_IRQn 可由 SD_CD 和 AXP2101_IRQ 共享。HAL 在调用本函数前
  *         已分别检查并清除对应引脚的挂起位，因此这里只负责引脚到语义源的映射。
  *         后续 cb 仍运行在 ISR 上下文，只允许记录事件或使用 ISR-safe 通知。
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SD_CD_Pin)
    {
        Board_IRQ_DispatchFromISR(BOARD_IRQ_SOURCE_SD_DETECT);
    }
    else if (GPIO_Pin == AXP2101_IRQ_Pin)
    {
        Board_IRQ_DispatchFromISR(BOARD_IRQ_SOURCE_PMIC);
    }
}
