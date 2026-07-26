#ifndef LOG_USB_CDC_ADAPTER_H
#define LOG_USB_CDC_ADAPTER_H

#include <stdint.h>

#include "Components/log/log_adapter.h"
#include "Components/log/log_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LOG_USB_CDC_ADAPTER_ANSI_COLOR_ENABLE
#define LOG_USB_CDC_ADAPTER_ANSI_COLOR_ENABLE 1U
#endif

#define LOG_USB_CDC_ADAPTER_RAM_BUFFER_SIZE LOG_FORMAT_BUFFER_SIZE
#define LOG_USB_CDC_ADAPTER_TX_BUFFER_SIZE  (LOG_FORMAT_BUFFER_SIZE + 16U)

/**
  * @brief USB CDC 日志 Adapter 的持久上下文。
  * @note  USB 异步发送完成前仍会读取 TxBuffer，因此该对象必须由 Platform
  *        长期持有，不能定义成 Bind 函数的局部变量。
  */
typedef struct
{
    char LastMessage[LOG_USB_CDC_ADAPTER_RAM_BUFFER_SIZE];
    uint32_t LastMessageLength;
    uint32_t WriteCount;
    uint8_t TxBuffer[LOG_USB_CDC_ADAPTER_TX_BUFFER_SIZE];
} LOG_UsbCDCAdapterTypeDef;

LOG_StatusTypeDef LOG_UsbCDCAdapter_Bind(
    LOG_UsbCDCAdapterTypeDef *adapter,
    LOG_OutputTypeDef *output,
    LOG_TimeSourceTypeDef *time_source);

#ifdef __cplusplus
}
#endif

#endif /* LOG_USB_CDC_ADAPTER_H */
