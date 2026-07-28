/**
  ******************************************************************************
  * @file    log_adapter.h
  * @brief   日志核心初始化所需的输出和时间源 Adapter Interface。
  ******************************************************************************
  */

#ifndef LOG_ADAPTER_H
#define LOG_ADAPTER_H

#include "Components/log/log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief 输出 Adapter 的非阻塞提交结果。
  */
typedef enum
{
    LOG_OUTPUT_OK = 0U,   /**< 消息已被 Adapter 接受。 */
    LOG_OUTPUT_BUSY,      /**< Adapter 正忙，调用者应稍后重试。 */
    LOG_OUTPUT_NOT_READY, /**< Adapter 尚未就绪，调用者应稍后重试。 */
    LOG_OUTPUT_ERROR      /**< Adapter 发生不可恢复的提交错误。 */
} LOG_OutputStatusTypeDef;

/**
  * @brief 尝试非阻塞提交一条完整日志的输出函数类型。
  * @param Context 与输出 Ops 成对绑定的具体 Adapter 对象。
  * @param Level 当前消息等级，可用于选择 ANSI 颜色或输出通道。
  * @param Data 完整日志文本；在函数返回前必须保持只读。
  * @param Length 日志文本字节数，不包含结尾 '\0'。
  * @retval LOG_OutputStatusTypeDef 本次提交的归一化结果。
  */
typedef LOG_OutputStatusTypeDef (*LOG_OutputTryWriteFuncTypeDef)(
    void *Context,
    LOG_LevelTypeDef Level,
    const char *Data,
    uint32_t Length);

/**
  * @brief 日志输出 Adapter 必须实现的操作表。
  */
typedef struct
{
    LOG_OutputTryWriteFuncTypeDef TryWrite; /**< 非阻塞文本提交函数。 */
} LOG_OutputOpsTypeDef;

/**
  * @brief 已绑定的输出操作和具体 Adapter 上下文。
  */
typedef struct
{
    const LOG_OutputOpsTypeDef *Ops; /**< 输出操作表。 */
    void *Context;                   /**< 传递给输出函数的 Adapter 对象。 */
} LOG_OutputTypeDef;

/**
  * @brief 获取日志时间戳的函数类型。
  * @param Context 与时间源成对绑定的对象，可以为 NULL。
  * @return 自时间源起点开始累计的无符号毫秒值，允许自然回绕。
  */
typedef uint32_t (*LOG_GetTimeMsFuncTypeDef)(void *Context);

/**
  * @brief 已绑定的毫秒时间源和独立上下文。
  */
typedef struct
{
    LOG_GetTimeMsFuncTypeDef GetTimeMs; /**< 取得当前毫秒时间戳。 */
    void *Context;                      /**< 传递给时间函数的对象。 */
} LOG_TimeSourceTypeDef;

LOG_StatusTypeDef LOG_Init(const LOG_OutputTypeDef *Output,
                           const LOG_TimeSourceTypeDef *TimeSource);

#ifdef __cplusplus
}
#endif

#endif /* LOG_ADAPTER_H */
