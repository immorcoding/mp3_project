/**
  ******************************************************************************
  * @file    stm32_qspi_irq.h
  * @brief   STM32 HAL QSPI 异步操作回调的注册与分发接口。
  *
  * @details
  *          本 Adapter 通过 HAL_QSPI_RegisterCallback() 为具体 QSPI Handle
 *          安装接收完成、状态匹配、错误和中止回调。它只传播 HAL 已归类的 QSPI 事件，
  *          不包含 W25Qxx、Platform、FreeRTOS 或任何业务语义。
  ******************************************************************************
  */

#ifndef STM32_QSPI_IRQ_H
#define STM32_QSPI_IRQ_H

#include "stm32h7xx_hal.h"

/** @brief STM32 QSPI IRQ Adapter 的操作结果。 */
typedef enum
{
    STM32QSPIIRQ_OK = 0U,
    STM32QSPIIRQ_ERROR
} STM32QSPIIRQ_StatusTypeDef;

/** @brief 一个已注册 QSPI Handle 的异步操作生命周期事件。 */
typedef enum
{
    STM32QSPIIRQ_EVENT_READ_COMPLETE = 0U,
    STM32QSPIIRQ_EVENT_STATUS_MATCH,
    STM32QSPIIRQ_EVENT_ERROR,
    STM32QSPIIRQ_EVENT_ABORTED
} STM32QSPIIRQ_EventTypeDef;

/**
 * @brief 在 QSPI IRQ 上下文接收异步操作生命周期事件的函数类型。
 * @warning 实现只能执行常数时间、非阻塞且 FromISR 安全的操作。
 */
typedef void (*STM32QSPIIRQ_HandlerTypeDef)(STM32QSPIIRQ_EventTypeDef event,
                                            void *context);

/**
 * @brief 调用者长期持有的 QSPI IRQ 注册节点。
 * @note  节点采用侵入式链表；从 Register 成功到 Unregister 返回前，调用者不得
 *        移动、销毁或直接修改该对象。
 */
typedef struct STM32QSPIIRQ_Callback
{
    QSPI_HandleTypeDef *Handle;
    STM32QSPIIRQ_HandlerTypeDef Handler;
    void *Context;
    struct STM32QSPIIRQ_Callback *Next;
} STM32QSPIIRQ_CallbackTypeDef;

STM32QSPIIRQ_StatusTypeDef STM32QSPIIRQ_Register(
    STM32QSPIIRQ_CallbackTypeDef *callback,
    QSPI_HandleTypeDef *handle,
    STM32QSPIIRQ_HandlerTypeDef handler,
    void *context);
STM32QSPIIRQ_StatusTypeDef STM32QSPIIRQ_Unregister(
    STM32QSPIIRQ_CallbackTypeDef *callback);

#endif /* STM32_QSPI_IRQ_H */
