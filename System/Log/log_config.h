/**
  ******************************************************************************
  * @file    log_config.h
  * @brief   系统日志模块编译期配置。
  ******************************************************************************
  */

#ifndef LOG_CONFIG_H
#define LOG_CONFIG_H

/* Includes ------------------------------------------------------------------*/
#include "System/Log/log.h"

/* Exported constants --------------------------------------------------------*/
/** @brief 日志系统上电后的默认过滤等级。 */
#define LOG_DEFAULT_LEVEL           LOG_LEVEL_INFO

/** @brief 单条格式化日志的最大缓冲区大小，包含结尾的 '\0'。 */
#define LOG_FORMAT_BUFFER_SIZE      256U

/** @brief RAM 日志队列可同时保存的完整日志条数。 */
#define LOG_QUEUE_DEPTH             8U

/** @brief RAM 端口保存的最后一条日志大小，包含结尾的 '\0'。 */
#define LOG_PORT_RAM_BUFFER_SIZE    LOG_FORMAT_BUFFER_SIZE

/** @brief USB 串口终端是否启用 ANSI 字体颜色。 */
#define LOG_PORT_ANSI_COLOR_ENABLE  1U

/** @brief USB 异步发送缓冲区大小，额外空间用于 ANSI 颜色控制字符。 */
#define LOG_PORT_TX_BUFFER_SIZE     (LOG_FORMAT_BUFFER_SIZE + 16U)

#endif /* LOG_CONFIG_H */
