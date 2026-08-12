/**
  ******************************************************************************
  * @file    stm32_sdmmc_irq.c
  * @brief   STM32 HAL SDMMC DMA 回调注册和 Handle 匹配分发实现。
  *
  * @details
  *          CubeMX 生成的 FatFs BSP 可能保留全局 HAL_SD_*Callback() 弱回调桥。
  *          本模块不重定义那些全局符号，而是在每个已初始化 SD Handle 上使用 HAL
  *          回调注册功能安装自己的静态入口，因此不会与生成文件产生强符号冲突。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/irq/stm32_sdmmc_irq.h"

#include <stdbool.h>
#include <stddef.h>

/** @brief 已注册 SDMMC 事件订阅者的侵入式链表表头。 */
static STM32SDMMCIRQ_CallbackTypeDef * volatile hstm32_sdmmc_irq_callbacks;

/**
  * @brief  把一个源特定 SDMMC 事件分发给匹配 HAL Handle 的所有注册节点。
  * @param  handle HAL_SD_IRQHandler() 当前正在处理的 Handle。
  * @param  event 已由 HAL 归类的传输事件。
  * @note   本函数在中断上下文运行。注册/注销会短暂关闭普通中断，以保证链表迭代
  *         期间节点不会被并发移除；Handler 不得在回调中注销自身或修改任何节点。
  */
static void stm32_sdmmc_irq_dispatch(SD_HandleTypeDef *handle,
                                     STM32SDMMCIRQ_EventTypeDef event)
{
    STM32SDMMCIRQ_CallbackTypeDef *callback = hstm32_sdmmc_irq_callbacks;

    while (callback != NULL)
    {
        if ((callback->Handle == handle) && (callback->Handler != NULL))
        {
            callback->Handler(event, callback->Context);
        }

        callback = callback->Next;
    }
}

/** @brief HAL 注册的读 DMA 完成入口。 */
static void stm32_sdmmc_irq_read_complete(SD_HandleTypeDef *handle)
{
    stm32_sdmmc_irq_dispatch(handle, STM32SDMMCIRQ_EVENT_READ_COMPLETE);
}

/** @brief HAL 注册的写 DMA 完成入口。 */
static void stm32_sdmmc_irq_write_complete(SD_HandleTypeDef *handle)
{
    stm32_sdmmc_irq_dispatch(handle, STM32SDMMCIRQ_EVENT_WRITE_COMPLETE);
}

/** @brief HAL 注册的 SDMMC 错误入口。 */
static void stm32_sdmmc_irq_error(SD_HandleTypeDef *handle)
{
    stm32_sdmmc_irq_dispatch(handle, STM32SDMMCIRQ_EVENT_ERROR);
}

/** @brief HAL 注册的 SDMMC 中止完成入口。 */
static void stm32_sdmmc_irq_abort(SD_HandleTypeDef *handle)
{
    stm32_sdmmc_irq_dispatch(handle, STM32SDMMCIRQ_EVENT_ABORTED);
}

/**
  * @brief  将本 Adapter 的四个 DMA 生命周期回调安装到一个 READY 的 HAL Handle。
  * @retval true 所有回调均已成功注册。
  * @retval false HAL Handle 状态不允许注册，或任一个 HAL 注册调用失败。
  * @note   STM32 HAL 仅允许在 HAL_SD_STATE_READY 注册这些回调。每次热插拔后
  *         HAL_SD_Init() 都可能重置回调指针，故 Platform 必须在新的介质初始化
  *         成功后再次调用 Register，而不能假定旧注册永久有效。
  */
static bool stm32_sdmmc_irq_install_callbacks(SD_HandleTypeDef *handle)
{
    if (HAL_SD_RegisterCallback(handle,
                                HAL_SD_RX_CPLT_CB_ID,
                                stm32_sdmmc_irq_read_complete) != HAL_OK)
    {
        return false;
    }

    if (HAL_SD_RegisterCallback(handle,
                                HAL_SD_TX_CPLT_CB_ID,
                                stm32_sdmmc_irq_write_complete) != HAL_OK)
    {
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_RX_CPLT_CB_ID);
        return false;
    }

    if (HAL_SD_RegisterCallback(handle,
                                HAL_SD_ERROR_CB_ID,
                                stm32_sdmmc_irq_error) != HAL_OK)
    {
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_TX_CPLT_CB_ID);
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_RX_CPLT_CB_ID);
        return false;
    }

    if (HAL_SD_RegisterCallback(handle,
                                HAL_SD_ABORT_CB_ID,
                                stm32_sdmmc_irq_abort) != HAL_OK)
    {
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_ERROR_CB_ID);
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_TX_CPLT_CB_ID);
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_RX_CPLT_CB_ID);
        return false;
    }

    return true;
}

/**
  * @brief  判断链表在排除一个节点后是否仍包含同一 HAL Handle 的订阅者。
  */
static bool stm32_sdmmc_irq_has_handle(SD_HandleTypeDef *handle)
{
    STM32SDMMCIRQ_CallbackTypeDef *cursor = hstm32_sdmmc_irq_callbacks;

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
  * @brief  为一个已初始化的 STM32 HAL SD Handle 注册事件处理节点。
  * @param  callback 调用者长期持有的注册节点。
  * @param  handle 已完成 HAL_SD_Init() 且处于 READY 的 HAL Handle。
  * @param  handler ISR 中调用的源特定事件处理函数。
  * @param  context 原样传给 handler 的调用者上下文。
  * @retval STM32SDMMCIRQ_OK 注册和 HAL 回调安装均成功。
  * @retval STM32SDMMCIRQ_ERROR 参数非法、重复注册或 HAL 回调安装失败。
  */
STM32SDMMCIRQ_StatusTypeDef STM32SDMMCIRQ_Register(
    STM32SDMMCIRQ_CallbackTypeDef *callback,
    SD_HandleTypeDef *handle,
    STM32SDMMCIRQ_HandlerTypeDef handler,
    void *context)
{
    STM32SDMMCIRQ_CallbackTypeDef *cursor;
    uint32_t primask;

    if ((callback == NULL) || (handle == NULL) || (handler == NULL) ||
        (HAL_SD_GetState(handle) != HAL_SD_STATE_READY))
    {
        return STM32SDMMCIRQ_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    cursor = hstm32_sdmmc_irq_callbacks;
    while (cursor != NULL)
    {
        if (cursor == callback)
        {
            if (primask == 0U)
            {
                __enable_irq();
            }

            return STM32SDMMCIRQ_ERROR;
        }

        cursor = cursor->Next;
    }

    if (!stm32_sdmmc_irq_install_callbacks(handle))
    {
        if (primask == 0U)
        {
            __enable_irq();
        }

        return STM32SDMMCIRQ_ERROR;
    }

    callback->Handle = handle;
    callback->Handler = handler;
    callback->Context = context;
    callback->Next = hstm32_sdmmc_irq_callbacks;
    hstm32_sdmmc_irq_callbacks = callback;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return STM32SDMMCIRQ_OK;
}

/**
  * @brief  注销一个 SDMMC 事件处理节点。
  * @param  callback 当前已注册的调用者节点。
  * @retval STM32SDMMCIRQ_OK 节点已安全地从 ISR 可见链表移除。
  * @retval STM32SDMMCIRQ_ERROR 参数非法或节点尚未注册。
  * @note   若该 Handle 已经被热拔卡反初始化，HAL 不再处于 READY；此时仅移除
  *         本地节点即可。下次卡初始化成功后会重新注册，不会保留过期回调。
  */
STM32SDMMCIRQ_StatusTypeDef STM32SDMMCIRQ_Unregister(
    STM32SDMMCIRQ_CallbackTypeDef *callback)
{
    STM32SDMMCIRQ_CallbackTypeDef *cursor;
    STM32SDMMCIRQ_CallbackTypeDef *previous = NULL;
    SD_HandleTypeDef *handle;
    uint32_t primask;

    if (callback == NULL)
    {
        return STM32SDMMCIRQ_ERROR;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    cursor = hstm32_sdmmc_irq_callbacks;
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

        return STM32SDMMCIRQ_ERROR;
    }

    handle = callback->Handle;
    if (previous == NULL)
    {
        hstm32_sdmmc_irq_callbacks = callback->Next;
    }
    else
    {
        previous->Next = callback->Next;
    }

    callback->Handle = NULL;
    callback->Handler = NULL;
    callback->Context = NULL;
    callback->Next = NULL;

    if ((!stm32_sdmmc_irq_has_handle(handle)) &&
        (HAL_SD_GetState(handle) == HAL_SD_STATE_READY))
    {
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_RX_CPLT_CB_ID);
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_TX_CPLT_CB_ID);
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_ERROR_CB_ID);
        (void)HAL_SD_UnRegisterCallback(handle, HAL_SD_ABORT_CB_ID);
    }

    if (primask == 0U)
    {
        __enable_irq();
    }

    return STM32SDMMCIRQ_OK;
}
