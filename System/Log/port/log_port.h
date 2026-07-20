/**
  ******************************************************************************
  * @file    log_port.h
  * @brief   系统日志模块的本板端口绑定接口。
  *
  * @details
  *          当前端口使用 USB CDC 输出日志，并使用 HAL_GetTick() 提供毫秒
  *          时间戳。USB Adapter 的就绪判断、异步发送缓冲区和 ANSI 颜色
  *          均封装在 log_port.c，日志核心与应用层不依赖 USB 细节。
  *          本头文件属于日志模块内部接口，不是 Application 层 API。
  ******************************************************************************
  */

#ifndef LOG_PORT_H
#define LOG_PORT_H

/* Includes ------------------------------------------------------------------*/
#include "System/Log/log_internal.h"

/* Internal functions --------------------------------------------------------*/
/**
  * @brief  为默认日志实例绑定本工程的 USB CDC 输出端口和 HAL 毫秒时间源。
  * @param  hlog 待绑定的内部日志 Handle。
  * @note   只装配函数表/上下文并清空端口上下文，不枚举 USB，也不发送数据。
  * @retval LOG_OK    绑定成功。
  * @retval LOG_ERROR hlog 为空。
  */
LOG_StatusTypeDef LOG_Port_Bind(LOG_HandleTypeDef *hlog);

#endif /* LOG_PORT_H */
