/**
  ******************************************************************************
  * @file    log_usb_cdc_stm32_hal_adapter.h
  * @brief   USB CDC 日志 Adapter 的持久 Context 和绑定接口。
  ******************************************************************************
  */

#ifndef LOG_USB_CDC_STM32_HAL_ADAPTER_H
#define LOG_USB_CDC_STM32_HAL_ADAPTER_H

#include <stdint.h>

#include "Components/log/log_adapter.h"
#include "Components/log/log_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LOG_USB_CDC_STM32_HAL_ADAPTER_ANSI_COLOR_ENABLE
/** @brief 是否在 USB CDC 日志前后加入 ANSI 颜色转义序列。 */
#define LOG_USB_CDC_STM32_HAL_ADAPTER_ANSI_COLOR_ENABLE 1U
#endif

/** @brief 调试器可直接观察的最近一条原始日志缓冲区大小。 */
#define LOG_USB_CDC_STM32_HAL_ADAPTER_RAM_BUFFER_SIZE LOG_FORMAT_BUFFER_SIZE

/** @brief USB 实际发送缓冲区大小，额外空间用于 ANSI 前缀和复位序列。 */
#define LOG_USB_CDC_STM32_HAL_ADAPTER_TX_BUFFER_SIZE  (LOG_FORMAT_BUFFER_SIZE + 16U)

/**
  * @brief USB CDC 日志 Adapter 的持久上下文。
  * @note  USB 异步发送完成前仍会读取 TxBuffer，因此该对象必须由 Platform
  *        长期持有，不能定义成 Bind 函数的局部变量。
  */
typedef struct
{
    char LastMessage[LOG_USB_CDC_STM32_HAL_ADAPTER_RAM_BUFFER_SIZE]; /**< 最近一次提交的无颜色原始日志副本。 */
    uint32_t LastMessageLength; /**< LastMessage 的有效字节数，不包含结尾 '\0'。 */
    uint32_t WriteCount;        /**< 自 Bind 起被 USB CDC 接受的日志累计数量。 */
    uint8_t TxBuffer[LOG_USB_CDC_STM32_HAL_ADAPTER_TX_BUFFER_SIZE]; /**< USB 异步传输期间必须保持有效的实际发送数据。 */
} LOG_UsbCDC_STM32HALAdapterTypeDef;

LOG_StatusTypeDef LOG_UsbCDC_STM32HALAdapter_Bind(
    LOG_UsbCDC_STM32HALAdapterTypeDef *adapter,
    LOG_OutputTypeDef *output,
    LOG_TimeSourceTypeDef *time_source);

#ifdef __cplusplus
}
#endif

#endif /* LOG_USB_CDC_STM32_HAL_ADAPTER_H */
