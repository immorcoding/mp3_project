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
