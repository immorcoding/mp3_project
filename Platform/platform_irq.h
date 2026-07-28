#ifndef PLATFORM_IRQ_H
#define PLATFORM_IRQ_H

#include "Platform/platform.h"

/**
  * @brief 本板支持的逻辑中断源。
  */
typedef enum
{
    PLATFORM_IRQ_SOURCE_SD_DETECT = 0,
    PLATFORM_IRQ_SOURCE_PMIC,
    PLATFORM_IRQ_SOURCE_COUNT
} Platform_IRQ_SourceTypeDef;

/**
  * @brief Platform Module 为一个逻辑中断源注册的 ISR 回调。
  * @note  回调运行在 ISR 上下文，只允许执行 ISR 安全操作。
  */
typedef void (*Platform_IRQ_CallbackTypeDef)(void *context);

Platform_StatusTypeDef Platform_IRQ_Init(void);

Platform_StatusTypeDef Platform_IRQ_Register(
    Platform_IRQ_SourceTypeDef source,
    Platform_IRQ_CallbackTypeDef callback,
    void *context);

Platform_StatusTypeDef Platform_IRQ_Unregister(
    Platform_IRQ_SourceTypeDef source);

#endif /* PLATFORM_IRQ_H */
