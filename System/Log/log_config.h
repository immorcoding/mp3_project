/**
  ******************************************************************************
  * @file    log_config.h
  * @brief   系统日志模块编译期配置。
  ******************************************************************************
  */

#ifndef LOG_CONFIG_H
#define LOG_CONFIG_H

/* Includes ------------------------------------------------------------------*/
#include "Log/log.h"

/* Exported constants --------------------------------------------------------*/
/** @brief 日志系统上电后的默认过滤等级。 */
#define LOG_DEFAULT_LEVEL           LOG_LEVEL_INFO

/** @brief 单条格式化日志的最大缓冲区大小，包含结尾的 '\0'。 */
#define LOG_FORMAT_BUFFER_SIZE      256U

/** @brief RAM 端口保存的最后一条日志大小，包含结尾的 '\0'。 */
#define LOG_PORT_RAM_BUFFER_SIZE    LOG_FORMAT_BUFFER_SIZE

#endif /* LOG_CONFIG_H */
