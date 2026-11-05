/**
  ******************************************************************************
  * @file    log_service.h
  * @brief   FreeRTOS 异步日志消息块投递与消费 Interface。
  ******************************************************************************
  */

#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#include <stdint.h>

#include "Service/service.h"
#include "Service/log/log_service_config.h"

/** @brief Service_Log 对上层公开的日志等级。 */
typedef enum
{
    SERVICE_LOG_LEVEL_NONE = 0U, /**< 不投递任何等级。 */
    SERVICE_LOG_LEVEL_ERROR,     /**< 严重错误。 */
    SERVICE_LOG_LEVEL_WARN,      /**< 警告。 */
    SERVICE_LOG_LEVEL_INFO,      /**< 正常运行信息。 */
    SERVICE_LOG_LEVEL_DEBUG      /**< 调试详细信息。 */
} Service_Log_LevelTypeDef;

/** @brief 开机以来的日志丢失累计，供 Monitor 周期批次输出（DIAG-6）。 */
typedef struct
{
    uint32_t ServiceDropped; /**< Service_Log_Post() 没能交给 Log task 的条数（消息池满、队列转移失败、文本过长）。 */
    uint32_t CoreDropped;    /**< Log 核心队列溢出或发送错误的丢弃数（`LOG_StatsTypeDef.DroppedCount` 的副本，由 Log task 刷新）。 */
} Service_Log_DropStatsTypeDef;

Service_StatusTypeDef Service_Log_Init(void);
/**
 * @brief  读取日志丢失累计。
 * @note   任意普通任务可调用，不碰日志核心（核心只由 Log task 访问，DIAG-1）：`CoreDropped` 是
 *         Log task 每次 `Service_Log_Consume()` 刷新的副本。两项各自单调不降，可能不是同一时刻的快照。
 */
Service_StatusTypeDef Service_Log_GetDropStats(Service_Log_DropStatsTypeDef *stats);
/**
 * @brief  将一条已格式化消息异步投递给 Log task。
 * @note   本函数不生成最终输出时间戳；时间戳由 Log task 随后调用的
 *         Components/log `LOG_Printf()` 生成，故输出时间可能晚于本次投递。
 */
Service_StatusTypeDef Service_Log_Post(
    Service_Log_LevelTypeDef level,
    const char *tag,
    const char *text);
Service_StatusTypeDef Service_Log_Consume(void);

#endif /* LOG_SERVICE_H */
