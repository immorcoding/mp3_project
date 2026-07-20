/**
  ******************************************************************************
  * @file    log_port.c
  * @brief   系统日志模块的本板端口实现。
  *
  * @details
  *          当前实现将最后一条完整日志保存到 RAM 快照对象，并使用
  *          HAL_GetTick() 返回毫秒时间戳。输出后端与时间源在
  *          LOG_Port_Bind() 中一次性注入日志 Handle。
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
    uint32_t WriteCount;                        /*!< 成功写入 RAM 快照的累计次数。 */
} LOG_PortContextTypeDef;

/* Private variables ---------------------------------------------------------*/
/** @brief 输出对象；可在调试器中监视本符号。 */
static LOG_PortContextTypeDef log_port_context;

/* Private functions ---------------------------------------------------------*/
/**
    * @brief  将日志写入 RAM 快照。
    * @param  Context 日志端口上下文对象。
    * @param  Data    待写入的日志文本缓冲区。
    * @param  Length  文本长度，不包含结尾的 '\0'。
    * @retval LOG_OK    写入成功。
    * @retval LOG_ERROR 参数或状态无效。
    */
static LOG_StatusTypeDef LOG_PortWrite(void *Context,
                                    const char *Data,
                                    uint32_t Length)
{
    LOG_PortContextTypeDef *context = (LOG_PortContextTypeDef *)Context;

    if ((context == NULL) ||
        (Data == NULL) ||
        (Length == 0U) ||
        (Length >= (uint32_t)sizeof(context->LastMessage)))
    {
        return LOG_ERROR;
    }

    memcpy(context->LastMessage, Data, Length);
    context->LastMessage[Length] = '\0';
    context->LastMessageLength = Length;
    context->WriteCount++;

/*******************若更改 输出外设 需要修改此部分****************/
    CDC_Transmit_FS((uint8_t *)Data, Length);
/**************************************************************/

    return LOG_OK;
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
    .Write = LOG_PortWrite
};

/* Internal functions --------------------------------------------------------*/
/**
  * @brief  绑定本板采用的 RAM 输出后端和 HAL 毫秒时间源。
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
