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
  ******************************************************************************
  */

#ifndef LOG_BACKEND_H
#define LOG_BACKEND_H

/* Includes ------------------------------------------------------------------*/
#include "System/Log/log_internal.h"

/* Internal functions --------------------------------------------------------*/
/**
  * @brief  为默认日志实例绑定本工程选定的输出后端和毫秒时间源。
  * @param  hlog 待绑定的内部日志 Handle。
  * @note   当前 Implementation 选择 USB CDC Backend 和 HAL_GetTick()。
  *         本函数只装配函数表/上下文并复位 Backend 私有状态，不枚举 USB，
  *         也不发送数据。
  * @retval LOG_OK    绑定成功。
  * @retval LOG_ERROR hlog 为空。
  */
LOG_StatusTypeDef LOG_Backend_BindDefault(LOG_HandleTypeDef *hlog);

#endif /* LOG_BACKEND_H */
