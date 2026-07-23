/**
  ******************************************************************************
  * @file    log_backend.h
  * @brief   系统日志模块的默认输出后端绑定接口。
  *
  * @details
  *          当前默认后端使用 USB CDC 输出日志，并使用 HAL_GetTick() 提供
  *          毫秒时间戳。USB CDC Adapter 的就绪判断、状态转换、异步发送
  *          缓冲区和 ANSI 颜色均封装在 log_backend_usb_cdc.c。
  *
  *          日志核心只通过本文件的 Interface 完成默认绑定，不依赖 USB
  *          Device 类型或 ST USB 状态码。以后替换或增加 UART、RTT、文件
  *          等后端时，应保持日志核心的队列和格式化实现不变。
  *          本头文件属于日志模块内部接口，不是 Application 层 API。
  *          Bind 只成对安装 Backend Ops、Context 和时间源，不发送日志。
  ******************************************************************************
  */

#ifndef LOG_BACKEND_H
#define LOG_BACKEND_H

/* Includes ------------------------------------------------------------------*/
#include "System/Log/log_internal.h"

/* Internal functions --------------------------------------------------------*/
LOG_StatusTypeDef LOG_Backend_BindDefault(LOG_HandleTypeDef *hlog);

#endif /* LOG_BACKEND_H */
