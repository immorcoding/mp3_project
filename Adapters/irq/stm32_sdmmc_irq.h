/**
  ******************************************************************************
  * @file    stm32_sdmmc_irq.h
  * @brief   STM32 HAL SDMMC DMA 回调的注册与源特定分发接口。
  *
  * @details
  *          本 Adapter 通过 HAL_SD_RegisterCallback() 为具体 SD_HandleTypeDef
  *          安装接收完成、发送完成、错误和中止回调。它只传播原始 SDMMC 传输事件，
  *          不包含 Platform 语义、FreeRTOS 类型或任何文件系统操作。
  *
  *          与 GPIO EXTI 不同，SDMMC 回调以 HAL SD Handle 为匹配键。因此二者共享
  *          Adapters/irq 的目录职责，但保留各自强类型接口，避免一个 void * 通用
  *          中断框架丢失外设事件的参数与生命周期约束。
  ******************************************************************************
  */

#ifndef STM32_SDMMC_IRQ_H
#define STM32_SDMMC_IRQ_H

#include "stm32h7xx_hal.h"

/**
  * @brief STM32 SDMMC IRQ Adapter 操作结果。
  */
typedef enum
{
    STM32SDMMCIRQ_OK = 0U,
    STM32SDMMCIRQ_ERROR
} STM32SDMMCIRQ_StatusTypeDef;

/**
  * @brief 一个已注册 SDMMC Handle 的 DMA 生命周期事件。
  */
typedef enum
{
    STM32SDMMCIRQ_EVENT_READ_COMPLETE = 0U,
    STM32SDMMCIRQ_EVENT_WRITE_COMPLETE,
    STM32SDMMCIRQ_EVENT_ERROR,
    STM32SDMMCIRQ_EVENT_ABORTED
} STM32SDMMCIRQ_EventTypeDef;

/**
  * @brief 在 SDMMC IRQ 上下文接收传输生命周期事件的函数类型。
  * @warning 实现只能执行常数时间、非阻塞且 FromISR 安全的操作。
  */
typedef void (*STM32SDMMCIRQ_HandlerTypeDef)(STM32SDMMCIRQ_EventTypeDef event,
                                             void *context);

/**
  * @brief 调用者长期持有的 SDMMC IRQ 注册节点。
  * @note  节点采用侵入式链表；从 Register 成功到 Unregister 返回前，调用者不得
  *        移动、销毁或直接修改该对象。
  */
typedef struct STM32SDMMCIRQ_Callback
{
    SD_HandleTypeDef *Handle;
    STM32SDMMCIRQ_HandlerTypeDef Handler;
    void *Context;
    struct STM32SDMMCIRQ_Callback *Next;
} STM32SDMMCIRQ_CallbackTypeDef;

STM32SDMMCIRQ_StatusTypeDef STM32SDMMCIRQ_Register(
    STM32SDMMCIRQ_CallbackTypeDef *callback,
    SD_HandleTypeDef *handle,
    STM32SDMMCIRQ_HandlerTypeDef handler,
    void *context);

STM32SDMMCIRQ_StatusTypeDef STM32SDMMCIRQ_Unregister(
    STM32SDMMCIRQ_CallbackTypeDef *callback);

#endif /* STM32_SDMMC_IRQ_H */
