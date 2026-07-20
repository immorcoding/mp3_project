/**
  ******************************************************************************
  * @file    log_config.h
  * @brief   系统日志模块编译期配置。
  *
  * @details
  *          这些宏共同决定日志 RAM 占用、单条消息上限和端口显示策略。
  *          修改缓冲区大小时应同时检查 linker map 文件中的 RAM 使用量。
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

/**
  * @brief RAM 日志队列可同时保存的完整日志条数。
  * @note  队列主体约占 LOG_QUEUE_DEPTH * sizeof(LOG_MessageTypeDef)，当前
  *        每个槽位都拥有自己的完整文本副本。
  */
#define LOG_QUEUE_DEPTH             8U

/** @brief 调试快照 LastMessage 的大小，包含结尾的 '\0'。 */
#define LOG_PORT_RAM_BUFFER_SIZE    LOG_FORMAT_BUFFER_SIZE

/** @brief USB 串口终端是否启用 ANSI 字体颜色：1U 启用，0U 禁用。 */
#define LOG_PORT_ANSI_COLOR_ENABLE  1U

/**
  * @brief USB 异步发送缓冲区大小。
  * @note  比正文缓冲区多出的空间容纳 ANSI 颜色前缀和复位序列。USB CDC
  *        异步发送完成前会继续引用此缓冲区，因此它必须具有静态生命周期。
  */
#define LOG_PORT_TX_BUFFER_SIZE     (LOG_FORMAT_BUFFER_SIZE + 16U)

#endif /* LOG_CONFIG_H */
