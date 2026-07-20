/**
  ******************************************************************************
  * @file    log_port.c
  * @brief   系统日志模块的本板端口实现。
  *
  * @details
  *          当前实现使用 USB CDC 作为非阻塞输出 Adapter，并使用
  *          HAL_GetTick() 返回毫秒时间戳。USB 未打开或正在发送时，日志核心
  *          会保留队首消息并在下次 LOG_Process() 时重试。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "System/Log/port/log_port.h"

#include <stddef.h>
#include <string.h>

#include "USB_DEVICE/App/usbd_cdc_if.h"
#include "System/Log/log_config.h"
#include "main.h"

/* Private types -------------------------------------------------------------*/
/** @brief RAM 日志快照对象，便于在调试器中直接监视最后一条日志。 */
typedef struct
{
    char LastMessage[LOG_PORT_RAM_BUFFER_SIZE]; /*!< 最后一条完整日志，以 '\0' 结尾。 */
    uint32_t LastMessageLength;                 /*!< 日志长度，不包含结尾的 '\0'。 */
    uint32_t WriteCount;                        /*!< 成功提交给 USB CDC 的累计次数。 */
    uint8_t TxBuffer[LOG_PORT_TX_BUFFER_SIZE];  /*!< USB 异步发送期间保持有效的缓冲区。 */
} LOG_PortContextTypeDef;

/* Private variables ---------------------------------------------------------*/
/** @brief 输出对象；可在调试器中监视本符号。 */
static LOG_PortContextTypeDef log_port_context;

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  获取日志等级对应的 ANSI 前景色控制字符串。
  * @param  Level 当前消息等级。
  * @retval const char * ANSI 控制字符串；禁用颜色时返回空字符串。
  */
static const char *LOG_PortGetAnsiColor(LOG_LevelTypeDef Level)
{
#if LOG_PORT_ANSI_COLOR_ENABLE == 1U
    switch (Level)
    {
        case LOG_LEVEL_ERROR:
            return "\x1B[31m";

        case LOG_LEVEL_WARN:
            return "\x1B[33m";

        case LOG_LEVEL_INFO:
            return "\x1B[32m";

        case LOG_LEVEL_DEBUG:
            return "\x1B[36m";

        default:
            return "";
    }
#else
    (void)Level;
    return "";
#endif /* LOG_PORT_ANSI_COLOR_ENABLE */
}

/**
  * @brief  尝试把一条日志提交给 USB CDC。
  * @param  Context 日志端口上下文对象。
  * @param  Level   当前消息等级。
  * @param  Data    待发送日志文本。
  * @param  Length  文本长度，不包含结尾的 '\0'。
  * @note   USB 未打开或仍在发送上一条消息时不会阻塞，日志核心稍后重试。
  * @retval LOG_OUTPUT_OK        USB 已接收本次异步发送请求。
  * @retval LOG_OUTPUT_NOT_READY USB 尚未枚举、串口未打开或发送仍忙。
  * @retval LOG_OUTPUT_ERROR     参数、长度或 USB 提交发生错误。
  */
static LOG_OutputStatusTypeDef LOG_PortTryWrite(void *Context,
                                                LOG_LevelTypeDef Level,
                                                const char *Data,
                                                uint32_t Length)
{
    LOG_PortContextTypeDef *context = (LOG_PortContextTypeDef *)Context;
    const char *color;
    const char *reset;
    size_t color_length;
    size_t reset_length;
    size_t tx_length;
    uint8_t usb_status;

    if ((context == NULL) ||
        (Data == NULL) ||
        (Length == 0U) ||
        (Length >= (uint32_t)sizeof(context->LastMessage)))
    {
        return LOG_OUTPUT_ERROR;
    }

    if (CDC_IsReady_FS() == 0U)
    {
        return LOG_OUTPUT_NOT_READY;
    }

    color = LOG_PortGetAnsiColor(Level);
#if LOG_PORT_ANSI_COLOR_ENABLE == 1U
    reset = "\x1B[0m";
#else
    reset = "";
#endif /* LOG_PORT_ANSI_COLOR_ENABLE */

    color_length = strlen(color);
    reset_length = strlen(reset);
    tx_length = color_length + (size_t)Length + reset_length;

    if (tx_length > sizeof(context->TxBuffer))
    {
        return LOG_OUTPUT_ERROR;
    }

    (void)memcpy(context->TxBuffer, color, color_length);
    (void)memcpy(&context->TxBuffer[color_length], Data, Length);
    (void)memcpy(&context->TxBuffer[color_length + Length], reset, reset_length);

    usb_status = CDC_Transmit_FS(context->TxBuffer, (uint16_t)tx_length);
    if (usb_status == USBD_BUSY)
    {
        return LOG_OUTPUT_BUSY;
    }

    if (usb_status != USBD_OK)
    {
        return LOG_OUTPUT_ERROR;
    }

    (void)memcpy(context->LastMessage, Data, Length);
    context->LastMessage[Length] = '\0';
    context->LastMessageLength = Length;
    context->WriteCount++;

    return LOG_OUTPUT_OK;
}

/**
  * @brief  获取 HAL 系统节拍对应的毫秒时间戳。
  * @param  Context 当前实现不使用该参数，允许为 NULL。
  * @retval uint32_t HAL_GetTick() 返回的毫秒计数值。
  */
static uint32_t LOG_PortGetTimeMs(void *Context)
{
    (void)Context;
    return HAL_GetTick();
}

/** @brief 当前日志端口的输出操作表。 */
static const LOG_OutputOpsTypeDef log_port_output_ops =
{
    .TryWrite = LOG_PortTryWrite
};

/* Internal functions --------------------------------------------------------*/
/**
  * @brief  绑定本板采用的 USB CDC 输出 Adapter 和 HAL 毫秒时间源。
  * @param  hlog 待绑定的内部日志 Handle。
  * @note   每次调用都会清空 RAM 日志快照及其写入计数。
  * @retval LOG_OK    绑定成功。
  * @retval LOG_ERROR hlog 为空。
  */
LOG_StatusTypeDef LOG_Port_Bind(LOG_HandleTypeDef *hlog)
{
    if (hlog == NULL)
    {
        return LOG_ERROR;
    }

    (void)memset(&log_port_context, 0, sizeof(log_port_context));

    hlog->Output.Ops = &log_port_output_ops;
    hlog->Output.Context = &log_port_context;
    hlog->TimeSource.GetTimeMs = LOG_PortGetTimeMs;
    hlog->TimeSource.Context = NULL;

    return LOG_OK;
}
