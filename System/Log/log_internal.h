/**
  ******************************************************************************
  * @file    log_internal.h
  * @brief   系统日志模块内部类型定义。
  *
  * @details
  *          本文件只供 log.c 与日志 Port 使用。应用层和普通功能模块应仅
  *          包含 log.h，不应依赖本文件中的 Handle、Ops 或 Context 类型。
  ******************************************************************************
  */

#ifndef LOG_INTERNAL_H
#define LOG_INTERNAL_H

/* Includes ------------------------------------------------------------------*/
#include "System/Log/log_config.h"

/* Internal types ------------------------------------------------------------*/
/**
 * @brief 输出 Adapter 的非阻塞提交结果。
 */
typedef enum
{
    LOG_OUTPUT_OK = 0U,   /*!< 消息已被 Adapter 接收。 */
    LOG_OUTPUT_BUSY,      /*!< Adapter 正忙，消息应保留并稍后重试。 */
    LOG_OUTPUT_NOT_READY, /*!< Adapter 尚未就绪，消息应保留并稍后重试。 */
    LOG_OUTPUT_ERROR      /*!< Adapter 发生不可恢复的提交错误。 */
} LOG_OutputStatusTypeDef;

/**
  * @brief 底层日志非阻塞提交函数类型。
  * @param Context 输出后端对象上下文。
  * @param Level   当前消息等级，供终端颜色等输出策略使用。
  * @param Data    待输出文本。
  * @param Length  文本长度，不包含结尾的 '\0'。
  * @retval LOG_OUTPUT_OK        消息已被 Adapter 接收。
  * @retval LOG_OUTPUT_BUSY      Adapter 正忙。
  * @retval LOG_OUTPUT_NOT_READY Adapter 尚未就绪。
  * @retval LOG_OUTPUT_ERROR     Adapter 提交失败。
  */
typedef LOG_OutputStatusTypeDef (*LOG_OutputTryWriteFuncTypeDef)(
    void *Context,
    LOG_LevelTypeDef Level,
    const char *Data,
    uint32_t Length);

/** @brief 输出后端操作表。 */
typedef struct
{
    LOG_OutputTryWriteFuncTypeDef TryWrite; /*!< 非阻塞文本提交函数。 */
} LOG_OutputOpsTypeDef;

/** @brief 已绑定的输出后端及其对象上下文。 */
typedef struct
{
    const LOG_OutputOpsTypeDef *Ops; /*!< 输出后端操作表。 */
    void *Context;                   /*!< 传给输出函数的对象上下文。 */
} LOG_OutputTypeDef;

/**
  * @brief 毫秒时间源函数类型。
  * @param Context 时间源对象上下文。
  * @retval uint32_t 当前毫秒时间戳，允许自然溢出。
  */
typedef uint32_t (*LOG_GetTimeMsFuncTypeDef)(void *Context);

/** @brief 已绑定的时间源及其对象上下文。 */
typedef struct
{
    LOG_GetTimeMsFuncTypeDef GetTimeMs; /*!< 获取毫秒时间戳的函数。 */
    void *Context;                      /*!< 传给时间函数的对象上下文。 */
} LOG_TimeSourceTypeDef;

/** @brief RAM 队列中的一条完整日志。 */
typedef struct
{
    char Data[LOG_FORMAT_BUFFER_SIZE]; /*!< 日志正文，以 '\0' 结尾。 */
    uint16_t Length;                   /*!< 正文长度，不包含 '\0'。 */
    LOG_LevelTypeDef Level;            /*!< 消息等级。 */
} LOG_MessageTypeDef;

/** @brief 默认日志实例持有的固定槽位 RAM 队列。 */
typedef struct
{
    LOG_MessageTypeDef Messages[LOG_QUEUE_DEPTH]; /*!< 日志槽位。 */
    uint16_t Head;                                /*!< 下一条待发送消息索引。 */
    uint16_t Tail;                                /*!< 下一条新消息写入索引。 */
    uint16_t Count;                               /*!< 当前待发送消息数。 */
    uint32_t EnqueuedCount;                       /*!< 累计入队消息数。 */
    uint32_t SentCount;                           /*!< 累计提交给 Adapter 的消息数。 */
    uint32_t DroppedCount;                        /*!< 队列溢出或发送错误丢弃数。 */
    uint32_t OutputErrorCount;                    /*!< Adapter 提交错误累计数。 */
} LOG_QueueTypeDef;

/** @brief 默认日志对象的内部 Handle。 */
typedef struct
{
    LOG_OutputTypeDef Output;         /*!< 当前输出后端绑定。 */
    LOG_TimeSourceTypeDef TimeSource; /*!< 当前时间源绑定。 */
    LOG_QueueTypeDef Queue;           /*!< 等待输出的 RAM 日志队列。 */
    LOG_LevelTypeDef Level;           /*!< 当前过滤等级。 */
    LOG_StateTypeDef State;           /*!< 当前运行状态。 */
} LOG_HandleTypeDef;

#endif /* LOG_INTERNAL_H */
