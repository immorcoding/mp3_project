/**
  ******************************************************************************
  * @file    board_irq.h
  * @brief   板级逻辑中断源的回调注册与分发接口。
  *
  * @details
  *          每个逻辑中断源最多绑定一个 Callback 和 Context。注册不会覆盖
  *          已有绑定，必须先显式注销。Register/Unregister 只能在普通执行
  *          上下文调用；DispatchFromISR 及其调用的 Callback 运行在 ISR
  *          上下文，只允许执行 ISR 安全的事件记录操作。
  ******************************************************************************
  */

#ifndef BOARD_IRQ_H
#define BOARD_IRQ_H

#include "BSP/Board/board.h"

/**
  * @brief 本板支持的逻辑中断源。
  * @note 这里表达的是板级功能，不是具体 GPIO 引脚。
  */
typedef enum
{
    BOARD_IRQ_SOURCE_SD_DETECT = 0, /**< SD 卡检测逻辑中断源。 */
    BOARD_IRQ_SOURCE_PMIC,          /**< AXP2101 逻辑中断源。 */
    BOARD_IRQ_SOURCE_COUNT          /**< 逻辑中断源数量，仅用于范围和表容量。 */
} Board_IRQ_SourceTypeDef;

/**
  * @brief 板级中断回调函数类型。
  * @param context 注册回调时绑定的对象上下文，相当于 C++ 的 this。
  * @param source  触发回调的逻辑中断源。
  * @note  回调运行在中断上下文，只允许执行 ISR 安全操作。
  */
typedef void (*Board_IRQ_CallbackTypeDef)(void *context, Board_IRQ_SourceTypeDef source);

Board_StatusTypeDef Board_IRQ_Init(void);

Board_StatusTypeDef Board_IRQ_Register(Board_IRQ_SourceTypeDef source,
                                       Board_IRQ_CallbackTypeDef cb,
                                       void *context);

Board_StatusTypeDef Board_IRQ_Unregister(Board_IRQ_SourceTypeDef source);

void Board_IRQ_DispatchFromISR(Board_IRQ_SourceTypeDef source);

#endif /* BOARD_IRQ_H */
