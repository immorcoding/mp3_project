/**
  ******************************************************************************
  * @file    log_internal.h
  * @brief   系统日志模块内部类型定义。
  *
  * @details
  *          本文件只供 log.c 使用。Adapter 只依赖 log_adapter.h；应用层和普通功能模块应仅
  *          包含 log.h，不应依赖本文件中的 Handle、Ops 或 Context 类型。
  *          这些类型共同构成一个小型 C 语言对象模型：Handle 保存状态，
  *          Ops 保存可替换行为，Context 是传给该行为的具体对象指针。
  ******************************************************************************
  */

#ifndef LOG_INTERNAL_H
#define LOG_INTERNAL_H

/* Includes ------------------------------------------------------------------*/
#include "Components/log/log_adapter.h"
#include "Components/log/log_config.h"

/* Internal types ------------------------------------------------------------*/
/**
  * @brief RAM 队列中的一条完整日志槽位。
  * @note  Data 拥有消息副本，解决调用者栈缓冲区在异步发送前失效的问题。
  */
typedef struct
{
    char Data[LOG_FORMAT_BUFFER_SIZE]; /*!< 日志正文，以 '\0' 结尾。 */
    uint16_t Length;                   /*!< 正文长度，不包含 '\0'。 */
    LOG_LevelTypeDef Level;            /*!< 消息等级。 */
} LOG_MessageTypeDef;

/**
  * @brief 默认日志实例持有的固定槽位环形队列。
  * @details Head 指向最旧待发送消息，Tail 指向下一写入位置，Count 区分
  *          空队列与满队列。所有索引均以 LOG_QUEUE_DEPTH 取模回绕。
  */
typedef struct
{
    LOG_MessageTypeDef Messages[LOG_QUEUE_DEPTH]; /*!< 日志槽位。 */
    uint16_t Head;                                /*!< 下一条待发送消息索引。 */
    uint16_t Tail;                                /*!< 下一条新消息写入索引。 */
    uint16_t Count;                               /*!< 当前待发送消息数。 */
    uint32_t EnqueuedCount;                       /*!< 累计入队消息数。 */
    uint32_t SentCount;                           /*!< 累计被 Adapter 接受的消息数。 */
    uint32_t DroppedCount;                        /*!< 队列溢出或发送错误丢弃数。 */
    uint32_t OutputErrorCount;                    /*!< Adapter 提交错误累计数。 */
} LOG_QueueTypeDef;

/**
  * @brief 默认日志对象的内部 Handle。
  * @note  该类型不会出现在 log.h 中，应用只能通过公共函数操作唯一实例。
  */
typedef struct
{
    LOG_OutputTypeDef Output;         /*!< 当前输出后端绑定。 */
    LOG_TimeSourceTypeDef TimeSource; /*!< 当前时间源绑定。 */
    LOG_QueueTypeDef Queue;           /*!< 等待输出的 RAM 日志队列。 */
    LOG_LevelTypeDef Level;           /*!< 当前过滤等级。 */
    LOG_StateTypeDef State;           /*!< 当前运行状态。 */
} LOG_HandleTypeDef;

#endif /* LOG_INTERNAL_H */
