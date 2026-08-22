/**
  ******************************************************************************
  * @file    log_service.h
  * @brief   FreeRTOS 异步日志消息块投递与消费 Interface。
  ******************************************************************************
  */

#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#include "Service/service.h"

#define LOG_SERVICE_QUEUE_LENGTH 16U

/** @brief Service_Log 对上层公开的日志等级。 */
typedef enum
{
    SERVICE_LOG_LEVEL_NONE = 0U,
    SERVICE_LOG_LEVEL_ERROR,
    SERVICE_LOG_LEVEL_WARN,
    SERVICE_LOG_LEVEL_INFO,
    SERVICE_LOG_LEVEL_DEBUG
} Service_Log_LevelTypeDef;

Service_StatusTypeDef Service_Log_Init(void);
Service_StatusTypeDef Service_Log_Post(
    Service_Log_LevelTypeDef level,
    const char *tag,
    const char *text);
Service_StatusTypeDef Service_Log_Consume(void);

#endif /* LOG_SERVICE_H */
