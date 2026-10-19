/**
  ******************************************************************************
  * @file    log_sim.c
  * @brief   Service Log 替身：把音乐分区投递的日志同步打印到 stdout。
  *
  * @details
  *          模拟器没有 Log Task；只实现音乐分区用到的 Service_Log_Post()，输出
  *          不参与帧哈希，作为场景日志证据（如命令队列满）。
  ******************************************************************************
  */

#include "Service/log/log_service.h"

#include <stdio.h>

/**
 * @brief 同生产头：投递一条日志；替身立即打印 `[log] <等级> <tag>: <text>`。
 * @param[in] level 日志等级。
 * @param[in] tag 来源 tag；NULL 打印为空串。
 * @param[in] text 已格式化文本；NULL 打印为空串。
 * @return 始终为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Log_Post(
    Service_Log_LevelTypeDef level,
    const char *tag,
    const char *text)
{
    printf("[log] %d %s: %s\n",
           (int)level,
           (tag == NULL) ? "" : tag,
           (text == NULL) ? "" : text);
    return SERVICE_OK;
}
