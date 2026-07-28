/**
  ******************************************************************************
  * @file    platform_log.h
  * @brief   本板默认日志后端初始化的 Platform Interface。
  ******************************************************************************
  */

#ifndef PLATFORM_LOG_H
#define PLATFORM_LOG_H

#include "Platform/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

Platform_StatusTypeDef Platform_Log_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_LOG_H */
