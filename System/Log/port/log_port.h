/**
  ******************************************************************************
  * @file    log_port.h
  * @brief   系统日志模块的本板端口绑定接口。
  *
  * @details
  *          当前端口将日志输出到 RAM 快照，并使用 HAL_GetTick() 提供
  *          毫秒时间戳。以后切换 UART、USB 或其他时间源时，只需修改
  *          log_port.c，不需要修改日志核心与应用层。
  ******************************************************************************
  */

#ifndef LOG_PORT_H
#define LOG_PORT_H

/* Includes ------------------------------------------------------------------*/
#include "System/Log/log_internal.h"

/* Internal functions --------------------------------------------------------*/
/**
 * @brief 为默认日志实例绑定本工程的 RAM 输出后端和 HAL 毫秒时间源。
 * @param hlog 待绑定的内部日志 Handle。
 * @retval LOG_OK    绑定成功。
 * @retval LOG_ERROR hlog 为空。
 */
LOG_StatusTypeDef LOG_Port_Bind(LOG_HandleTypeDef *hlog);

#endif /* LOG_PORT_H */
