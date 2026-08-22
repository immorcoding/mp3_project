/**
  ******************************************************************************
  * @file    log_service.h
  * @brief   FreeRTOS 异步日志消息块投递与消费 Interface。
  ******************************************************************************
  */

#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#include "Components/log/log.h"

#define LOG_SERVICE_QUEUE_LENGTH 16U

LOG_StatusTypeDef Service_Log_Init(void);
LOG_StatusTypeDef Service_Log_Post(LOG_LevelTypeDef level, const char *tag, const char *text);
LOG_StatusTypeDef Service_Log_Consume(void);

#endif /* LOG_SERVICE_H */
