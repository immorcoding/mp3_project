/**
  ******************************************************************************
  * @file    log_backend_usb_cdc.c
  * @brief   系统日志模块的 USB CDC 输出后端实现。
  *
  * @details
  *          当前 Implementation 使用 USB CDC 作为非阻塞输出 Adapter，并
  *          使用 HAL_GetTick() 返回毫秒时间戳。USB 未打开或正在发送时，日志核心
  *          会保留队首消息并在下次 LOG_Process() 时重试。
  *
  *          本 Backend 有两类不同缓冲区：日志核心 Queue 保存“等待发送”的
  *          消息；本文件 TxBuffer 保存“已经交给 USB、但异步传输尚未完成”
  *          的消息。
  *          两者不能合并，否则队列槽位被复用时可能修改 USB 正在读取的数据。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "System/Log/backends/log_backend.h"

#include <stddef.h>
#include <string.h>

#include "USB_DEVICE/App/usbd_cdc_if.h"
#include "System/Log/log_config.h"
#include "main.h"

/* Private types -------------------------------------------------------------*/
/**
  * @brief USB CDC 日志后端的私有上下文。
  * @note  LastMessage/WriteCount 用于调试器观察；TxBuffer 则是保证 USB
  *        异步发送期间数据生命周期的功能性缓冲区。
  */
typedef struct
{
    char LastMessage[LOG_BACKEND_USB_CDC_RAM_BUFFER_SIZE]; /*!< 最后一条完整日志，以 '\0' 结尾。 */
    uint32_t LastMessageLength;                            /*!< 日志长度，不包含结尾的 '\0'。 */
    uint32_t WriteCount;                                   /*!< 成功提交给 USB CDC 的累计次数。 */
    uint8_t TxBuffer[LOG_BACKEND_USB_CDC_TX_BUFFER_SIZE];  /*!< USB 异步发送期间保持有效的缓冲区。 */
} LOG_UsbCdcBackendContextTypeDef;

/* Private variables ---------------------------------------------------------*/
/**
  * @brief 唯一 USB CDC 日志后端对象。
  * @note  static 阻止其他模块直接修改；需要观察时可由调试器按符号查看。
  */
static LOG_UsbCdcBackendContextTypeDef log_usb_cdc_backend_context;

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  获取日志等级对应的 ANSI 前景色控制字符串。
  * @param  Level 当前消息等级。
  * @retval const char * ANSI 控制字符串；禁用颜色时返回空字符串。
  */
static const char *LOG_BackendUsbCdc_GetAnsiColor(LOG_LevelTypeDef Level)
{
#if LOG_BACKEND_USB_CDC_ANSI_COLOR_ENABLE == 1U
    switch (Level)
    {
        case LOG_LEVEL_ERROR:
            /* ANSI SGR 31：红色。 */
            return "\x1B[31m";

        case LOG_LEVEL_WARN:
            /* ANSI SGR 33：黄色。 */
            return "\x1B[33m";

        case LOG_LEVEL_INFO:
            /* ANSI SGR 32：绿色。 */
            return "\x1B[32m";

        case LOG_LEVEL_DEBUG:
            /* ANSI SGR 36：青色。 */
            return "\x1B[36m";

        default:
            return "";
    }
#else
    (void)Level;
    return "";
#endif /* LOG_BACKEND_USB_CDC_ANSI_COLOR_ENABLE */
}

/**
  * @brief  将 ST USB Device 原始状态转换为日志模块统一的输出状态。
  * @param  NativeStatus CDC_Transmit_FS() 返回的原始状态值。
  * @note   函数 Interface 使用 int32_t，日志核心因此不需要认识
  *         USBD_StatusTypeDef。对 ST 状态的解释集中在本 Adapter 内部。
  * @retval LOG_OUTPUT_OK    USB Device 接受发送请求。
  * @retval LOG_OUTPUT_BUSY  USB Device 当前仍在处理上一笔发送。
  * @retval LOG_OUTPUT_ERROR 内存不足、USB 失败或未知原始状态。
  */
static LOG_OutputStatusTypeDef LOG_BackendUsbCdc_Result(int32_t NativeStatus)
{
    switch ((USBD_StatusTypeDef)NativeStatus)
    {
        case USBD_OK:
            return LOG_OUTPUT_OK;

        case USBD_BUSY:
            return LOG_OUTPUT_BUSY;

        case USBD_EMEM:
        case USBD_FAIL:
        default:
            return LOG_OUTPUT_ERROR;
    }
}

/**
  * @brief  尝试把一条日志提交给 USB CDC。
  * @param  Context USB CDC Backend 私有上下文对象。
  * @param  Level   当前消息等级。
  * @param  Data    待发送日志文本。
  * @param  Length  文本长度，不包含结尾的 '\0'。
  * @note   USB 未打开或仍在发送上一条消息时不会阻塞，日志核心稍后重试。
  * @retval LOG_OUTPUT_OK        USB 已接收本次异步发送请求。
  * @retval LOG_OUTPUT_BUSY      CDC_Transmit_FS() 发现发送端忙。
  * @retval LOG_OUTPUT_NOT_READY USB 未枚举、主机未置 DTR、稳定期未结束或忙。
  * @retval LOG_OUTPUT_ERROR     参数、长度或 USB 提交发生错误。
  */
static LOG_OutputStatusTypeDef LOG_BackendUsbCdc_TryWrite(void *Context,
                                                          LOG_LevelTypeDef Level,
                                                          const char *Data,
                                                          uint32_t Length)
{
    LOG_UsbCdcBackendContextTypeDef *context = (LOG_UsbCdcBackendContextTypeDef *)Context;
    LOG_OutputStatusTypeDef output_status;
    const char *color;
    const char *reset;
    size_t color_length;
    size_t reset_length;
    size_t tx_length;

    if ((context == NULL) ||
        (Data == NULL) ||
        (Length == 0U) ||
        (Length >= (uint32_t)sizeof(context->LastMessage)))
    {
        return LOG_OUTPUT_ERROR;
    }

    /*
     * 就绪判断包含枚举状态、DTR、打开后的稳定时间和 CDC TxState。
     * 任何条件不满足都属于可重试暂态，不能把队首日志丢掉。
     */
    if (CDC_IsReady_FS() == 0U)
    {
        return LOG_OUTPUT_NOT_READY;
    }

    /* 颜色只属于输出表现层，不写回核心队列，也不污染 LastMessage 快照。 */
    color = LOG_BackendUsbCdc_GetAnsiColor(Level);
#if LOG_BACKEND_USB_CDC_ANSI_COLOR_ENABLE == 1U
    reset = "\x1B[0m";
#else
    reset = "";
#endif /* LOG_BACKEND_USB_CDC_ANSI_COLOR_ENABLE */

    color_length = strlen(color);
    reset_length = strlen(reset);
    tx_length = color_length + (size_t)Length + reset_length;

    if (tx_length > sizeof(context->TxBuffer))
    {
        return LOG_OUTPUT_ERROR;
    }

    /*
     * 在具有静态生命周期的 TxBuffer 中按“颜色 + 正文 + 颜色复位”拼接。
     * USB Device 库异步持有该指针，发送完成前不得覆写该缓冲区。
     */
    (void)memcpy(context->TxBuffer, color, color_length);
    (void)memcpy(&context->TxBuffer[color_length], Data, Length);
    (void)memcpy(&context->TxBuffer[color_length + Length], reset, reset_length);

    /*
     * CDC_Transmit_FS() 返回 ST USB Device 原始状态。先在 Adapter 内转换为
     * 统一输出状态，再交还日志核心，避免 vendor 状态码跨越 Backend Seam。
     */
    output_status = LOG_BackendUsbCdc_Result((int32_t)CDC_Transmit_FS(context->TxBuffer,
                                                                     (uint16_t)tx_length));
    if (output_status != LOG_OUTPUT_OK)
    {
        return output_status;
    }

    /*
     * 只有 USB 接受请求后才更新调试快照；这里保存无 ANSI 字符的原始日志，
     * 便于 Memory Inspector 直接按 C 字符串查看。
     */
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
static uint32_t LOG_TimeSourceHal_GetTimeMs(void *Context)
{
    (void)Context;
    return HAL_GetTick();
}

/**
  * @brief USB CDC 日志后端的输出操作表。
  * @note  日志核心只持有该接口，不直接包含 USB_DEVICE 的类型。
  */
static const LOG_OutputOpsTypeDef log_usb_cdc_backend_ops = {
    .TryWrite = LOG_BackendUsbCdc_TryWrite
};

/* Internal functions --------------------------------------------------------*/
/**
  * @brief  绑定本工程默认采用的 USB CDC 输出 Adapter 和 HAL 毫秒时间源。
  * @param  hlog 待绑定的内部日志 Handle。
  * @note   每次调用都会清空 Backend 快照、异步发送缓冲区及写入计数；通常仅由
  *         LOG_Init() 调用一次。
  * @retval LOG_OK    绑定成功。
  * @retval LOG_ERROR hlog 为空。
  */
LOG_StatusTypeDef LOG_Backend_BindDefault(LOG_HandleTypeDef *hlog)
{
    if (hlog == NULL)
    {
        return LOG_ERROR;
    }

    /* 复位 Backend 私有状态，确保首次发送前 TxBuffer 和调试字段均已知。 */
    (void)memset(&log_usb_cdc_backend_context, 0, sizeof(log_usb_cdc_backend_context));

    /* Ops 与 Context 成对绑定：TryWrite 会把该指针还原为 Backend 上下文。 */
    hlog->Output.Ops = &log_usb_cdc_backend_ops;
    hlog->Output.Context = &log_usb_cdc_backend_context;

    /* 时间源独立绑定；当前 HAL_GetTick() 不需要对象，因此 Context 为 NULL。 */
    hlog->TimeSource.GetTimeMs = LOG_TimeSourceHal_GetTimeMs;
    hlog->TimeSource.Context = NULL;

    return LOG_OK;
}
