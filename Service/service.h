/**
  ******************************************************************************
  * @file    service.h
  * @brief   Service 层公开操作结果 Interface。
  ******************************************************************************
  */

#ifndef SERVICE_H
#define SERVICE_H

/**
  * @brief Service 对上层公开的通用操作结果。
  * @note  此枚举只表达调用者需要据此作出流程决策的语义；Platform、Component
  *        与 Middlewares 的原始错误码仅限各 Service 的 Implementation 使用。
  */
typedef enum
{
    SERVICE_OK = 0U,
    SERVICE_ERROR,
    SERVICE_INVALID_PARAM,
    SERVICE_NOT_READY,
    SERVICE_BUSY,
    SERVICE_TIMEOUT,
    SERVICE_NO_FILESYSTEM,
    SERVICE_INVALID_HANDLE,
    SERVICE_NO_MEDIA,
    SERVICE_NO_SPACE
} Service_StatusTypeDef;

#endif /* SERVICE_H */
