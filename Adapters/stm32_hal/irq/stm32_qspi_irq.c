/**
  ******************************************************************************
  * @file    stm32_qspi_irq.c
  * @brief   STM32 HAL QSPI 非阻塞传输回调的注册和 Handle 匹配分发实现。
  *
  * @details
  *          CubeMX 的 QSPI 向量只负责调用 HAL_QSPI_IRQHandler()。本模块使用
  *          Handle 局部回调注册接收 HAL 的读完成、错误和中止事件，因此不定义
  *          全局 HAL_QSPI_*Callback() 符号，也不会与其他 QSPI 使用者冲突。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/irq/stm32_qspi_irq.h"

#include <stdbool.h>
#include <stddef.h>

/** @brief 已注册 QSPI 事件订阅者的侵入式链表表头。 */
static STM32QSPIIRQ_CallbackTypeDef * volatile hstm32_qspi_irq_callbacks;

/**
 * @brief  把一个源特定 QSPI 事件分发给匹配 HAL Handle 的所有注册节点。
 * @param  handle HAL_QSPI_IRQHandler() 当前正在处理的 Handle。
 * @param  event 已由 HAL 归类的非阻塞读取生命周期事件。
 * @note   本函数运行在 IRQ 上下文。注册/注销会短暂关闭普通中断，以保证链表
 *         迭代期间节点不会被并发移除；Handler 不得在回调中注销自身或改动节点。
 */
static void stm32_qspi_irq_dispatch(QSPI_HandleTypeDef *handle,
                                    STM32QSPIIRQ_EventTypeDef event)
{
    STM32QSPIIRQ_CallbackTypeDef *callback = hstm32_qspi_irq_callbacks;

    while (callback != NULL)
    {
        if ((callback->Handle == handle) && (callback->Handler != NULL))
        {
            callback->Handler(event, callback->Context);
        }

        callback = callback->Next;
    }
}

/** @brief HAL 注册的 QSPI 间接读取完成入口。 */
static void stm32_qspi_irq_read_complete(QSPI_HandleTypeDef *handle)
{
    stm32_qspi_irq_dispatch(handle, STM32QSPIIRQ_EVENT_READ_COMPLETE);
}

/** @brief HAL 注册的 QSPI 传输错误入口。 */
static void stm32_qspi_irq_error(QSPI_HandleTypeDef *handle)
{
    stm32_qspi_irq_dispatch(handle, STM32QSPIIRQ_EVENT_ERROR);
}

/** @brief HAL 注册的 QSPI 中止完成入口。 */
static void stm32_qspi_irq_abort(QSPI_HandleTypeDef *handle)
{
    stm32_qspi_irq_dispatch(handle, STM32QSPIIRQ_EVENT_ABORTED);
}

/**
 * @brief  将本 Adapter 的 DMA 生命周期回调安装到一个 READY 的 HAL QSPI Handle。
 * @retval true 所有回调均已成功注册。
 * @retval false HAL Handle 状态不允许注册，或任一个 HAL 注册调用失败。
 * @note   HAL_QSPI_Init() 会复位 Handle 内的回调指针；当前 QSPI 不支持热插拔，
 *         因而 Platform Flash 只在启动识别成功后注册一次。若未来重初始化 QSPI，
 *         调用者必须先注销节点，再在新的 READY Handle 上重新注册。
 */
static bool stm32_qspi_irq_install_callbacks(QSPI_HandleTypeDef *handle)
{
    if (HAL_QSPI_RegisterCallback(handle,
                                  HAL_QSPI_RX_CPLT_CB_ID,
                                  stm32_qspi_irq_read_complete) != HAL_OK)
    {
        return false;
    }

    if (HAL_QSPI_RegisterCallback(handle,
                                  HAL_QSPI_ERROR_CB_ID,
                                  stm32_qspi_irq_error) != HAL_OK)
    {
        (void)HAL_QSPI_UnRegisterCallback(handle, HAL_QSPI_RX_CPLT_CB_ID);
        return false;
    }

    if (HAL_QSPI_RegisterCallback(handle,
                                  HAL_QSPI_ABORT_CB_ID,
                                  stm32_qspi_irq_abort) != HAL_OK)
    {
        (void)HAL_QSPI_UnRegisterCallback(handle, HAL_QSPI_ERROR_CB_ID);
        (void)HAL_QSPI_UnRegisterCallback(handle, HAL_QSPI_RX_CPLT_CB_ID);
        return false;
    }

    return true;
}

/**
 * @brief  判断链表在排除一个节点后是否仍包含同一 HAL Handle 的订阅者。
 */
static bool stm32_qspi_irq_has_handle(QSPI_HandleTypeDef *handle)
{
    STM32QSPIIRQ_CallbackTypeDef *cursor = hstm32_qspi_irq_callbacks;

    while (cursor != NULL)
    {
        if (cursor->Handle == handle)
        {
            return true;
        }

        cursor = cursor->Next;
    }

    return false;
}

/**
 * @brief  为一个 READY 的 STM32 HAL QSPI Handle 注册事件处理节点。
 * @param  callback 调用者长期持有的注册节点。
 * @param  handle 已完成 HAL_QSPI_Init() 且处于 READY 的 Handle。
 * @param  handler ISR 中调用的源特定事件处理函数。
 * @param  context 原样传给 handler 的调用者上下文。
 * @retval STM32QSPIIRQ_OK 注册和 HAL 回调安装均成功。
 * @retval STM32QSPIIRQ_ERROR 参数非法、重复注册或 HAL 回调安装失败。
 */
STM32QSPIIRQ_StatusTypeDef STM32QSPIIRQ_Register(
    STM32QSPIIRQ_CallbackTypeDef *callback,
    QSPI_HandleTypeDef *handle,
    STM32QSPIIRQ_HandlerTypeDef handler,
    void *context)
{
    STM32QSPIIRQ_CallbackTypeDef *cursor;
    uint32_t primask;

    if ((callback == NULL) || (handle == NULL) || (handler == NULL) ||
        (HAL_QSPI_GetState(handle) != HAL_QSPI_STATE_READY))
    {
        return STM32QSPIIRQ_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    cursor = hstm32_qspi_irq_callbacks;
    while (cursor != NULL)
    {
        if (cursor == callback)
        {
            if (primask == 0U)
            {
                __enable_irq();
            }

            return STM32QSPIIRQ_ERROR;
        }

        cursor = cursor->Next;
    }

    if (!stm32_qspi_irq_install_callbacks(handle))
    {
        if (primask == 0U)
        {
            __enable_irq();
        }

        return STM32QSPIIRQ_ERROR;
    }

    callback->Handle = handle;
    callback->Handler = handler;
    callback->Context = context;
    callback->Next = hstm32_qspi_irq_callbacks;
    hstm32_qspi_irq_callbacks = callback;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return STM32QSPIIRQ_OK;
}

/**
 * @brief  注销一个 QSPI 事件处理节点。
 * @param  callback 当前已注册的调用者节点。
 * @retval STM32QSPIIRQ_OK 节点已安全地从 ISR 可见链表移除。
 * @retval STM32QSPIIRQ_ERROR 参数非法或节点尚未注册。
 * @note   当前 QSPI 未提供运行期回收路径，但保留该函数以保持 IRQ Adapter 的
 *         注册生命周期完整，并为未来 QSPI 重初始化提供安全出口。
 */
STM32QSPIIRQ_StatusTypeDef STM32QSPIIRQ_Unregister(
    STM32QSPIIRQ_CallbackTypeDef *callback)
{
    STM32QSPIIRQ_CallbackTypeDef *cursor;
    STM32QSPIIRQ_CallbackTypeDef *previous = NULL;
    QSPI_HandleTypeDef *handle;
    uint32_t primask;

    if (callback == NULL)
    {
        return STM32QSPIIRQ_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    cursor = hstm32_qspi_irq_callbacks;
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

        return STM32QSPIIRQ_ERROR;
    }

    handle = callback->Handle;
    if (previous == NULL)
    {
        hstm32_qspi_irq_callbacks = callback->Next;
    }
    else
    {
        previous->Next = callback->Next;
    }

    callback->Handle = NULL;
    callback->Handler = NULL;
    callback->Context = NULL;
    callback->Next = NULL;

    if ((!stm32_qspi_irq_has_handle(handle)) &&
        (HAL_QSPI_GetState(handle) == HAL_QSPI_STATE_READY))
    {
        (void)HAL_QSPI_UnRegisterCallback(handle, HAL_QSPI_RX_CPLT_CB_ID);
        (void)HAL_QSPI_UnRegisterCallback(handle, HAL_QSPI_ERROR_CB_ID);
        (void)HAL_QSPI_UnRegisterCallback(handle, HAL_QSPI_ABORT_CB_ID);
    }

    if (primask == 0U)
    {
        __enable_irq();
    }

    return STM32QSPIIRQ_OK;
}
