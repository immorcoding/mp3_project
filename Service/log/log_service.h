#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#include "Components/log/log.h"

#define LOGMSG_QUEUE_LENGTH             16

LOG_StatusTypeDef Log_Service_Init(void);
LOG_StatusTypeDef LOG_Service_Post(LOG_LevelTypeDef Level, const char *tag, const char *text);
LOG_StatusTypeDef LOG_Service_Consume(void);

#endif