/**
  ******************************************************************************
  * @file    log_service.h
  * @brief   FreeRTOS 异步日志消息块投递与消费 Interface。
  ******************************************************************************
  */

#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

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

Service_StatusTypeDef Service_Log_Init(void);
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
