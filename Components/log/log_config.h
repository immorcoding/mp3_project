/**
  ******************************************************************************
  * @file    log_config.h
  * @brief   系统日志模块编译期配置。
  *
  * @details
  *          这些宏共同决定日志核心的 RAM 占用、单条消息上限和默认过滤等级。
  *          修改缓冲区大小时应同时检查 linker map 文件中的 RAM 使用量。
  ******************************************************************************
  */

#ifndef LOG_CONFIG_H
#define LOG_CONFIG_H

/* Includes ------------------------------------------------------------------*/
#include "Components/log/log.h"

/* Exported constants --------------------------------------------------------*/
#define LOG_DEFAULT_LEVEL           LOG_LEVEL_INFO /* 日志系统上电后的默认过滤等级。 */

#define LOG_FORMAT_BUFFER_SIZE      256U /* 单条格式化日志的最大缓冲区大小，包含结尾的 '\0'。 */

#define LOG_QUEUE_DEPTH             5U /* RAM 日志队列可同时保存的完整日志条数。 队列主体约占 LOG_QUEUE_DEPTH * sizeof(LOG_MessageTypeDef)，当前 每个槽位都拥有自己的完整文本副本。 */

#endif /* LOG_CONFIG_H */
