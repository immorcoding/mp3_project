/**
  ******************************************************************************
  * @file    log.c
  * @brief   系统日志核心功能实现。
  *
  * @details
  *          本文件保存唯一的默认日志对象，完成等级过滤、时间戳读取、
  *          文本格式化及底层输出调用。Handle 与端口绑定均不对应用层公开。
  ******************************************************************************
  */
#include "APP/app_config.h"

/* Includes ------------------------------------------------------------------*/
#include "System/Log/log.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "System/Log/log_config.h"
#include "System/Log/log_internal.h"
#include "System/Log/port/log_port.h"

/* Private variables ---------------------------------------------------------*/
/** @brief 默认日志实例，仅允许本文件中的公开函数访问。 */
static LOG_HandleTypeDef hlog_default;

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  将日志等级转换为输出前缀字符。
  * @param  Level 待转换的日志等级。
  * @retval 'E' 错误等级。
  * @retval 'W' 警告等级。
  * @retval 'I' 信息等级。
  * @retval 'D' 调试等级。
  * @retval '?' 非法或不支持的等级。
  */
static char LOG_LevelToChar(LOG_LevelTypeDef Level)
{
    switch (Level)
    {
        case LOG_LEVEL_ERROR:
            return 'E';

        case LOG_LEVEL_WARN:
            return 'W';

        case LOG_LEVEL_INFO:
            return 'I';

        case LOG_LEVEL_DEBUG:
            return 'D';

        default:
            return '?';
    }
}

/**
  * @brief  检查等级是否可用于一条实际日志消息。
  * @param  Level 待检查的消息等级。
  * @retval 1U Level 为 ERROR、WARN、INFO 或 DEBUG。
  * @retval 0U Level 为 NONE 或超出有效范围。
  */
static uint8_t LOG_IsMessageLevelValid(LOG_LevelTypeDef Level)
{
    return ((Level != LOG_LEVEL_NONE) &&
            ((uint32_t)Level <= (uint32_t)LOG_LEVEL_DEBUG)) ? 1U : 0U;
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化默认日志实例。
  * @note   本函数设置默认过滤等级，并通过 LOG_Port_Bind() 绑定 RAM 输出
  *         和毫秒时间源。重复调用会清空当前 RAM 日志快照。
  * @retval LOG_OK    初始化成功，日志状态变为 LOG_STATE_READY。
  * @retval LOG_ERROR 默认配置或端口接口无效，日志状态变为 LOG_STATE_ERROR。
  */
LOG_StatusTypeDef LOG_Init(void)
{
#if APP_LOG_ENABLE == 0
    return LOG_ERROR;
#endif /* APP_LOG_ENABLE == 0 */

    hlog_default.State = LOG_STATE_RESET;
    hlog_default.Level = LOG_DEFAULT_LEVEL;

    if ((uint32_t)hlog_default.Level > (uint32_t)LOG_LEVEL_DEBUG)
    {
        hlog_default.State = LOG_STATE_ERROR;
        return LOG_ERROR;
    }

    if (LOG_Port_Bind(&hlog_default) != LOG_OK)
    {
        hlog_default.State = LOG_STATE_ERROR;
        return LOG_ERROR;
    }

    if ((hlog_default.Output.Ops == NULL) ||
        (hlog_default.Output.Ops->Write == NULL) ||
        (hlog_default.TimeSource.GetTimeMs == NULL))
    {
        hlog_default.State = LOG_STATE_ERROR;
        return LOG_ERROR;
    }

    hlog_default.State = LOG_STATE_READY;
    return LOG_OK;
}

/**
  * @brief  修改默认日志实例的过滤等级。
  * @param  Level 新的过滤等级。
  * @retval LOG_OK    设置成功。
  * @retval LOG_ERROR 日志未就绪或 Level 非法。
  */
LOG_StatusTypeDef LOG_SetLevel(LOG_LevelTypeDef Level)
{
    if ((hlog_default.State != LOG_STATE_READY) ||
        ((uint32_t)Level > (uint32_t)LOG_LEVEL_DEBUG))
    {
        return LOG_ERROR;
    }

    hlog_default.Level = Level;
    return LOG_OK;
}

/**
  * @brief  获取默认日志实例的过滤等级。
  * @retval LOG_LevelTypeDef 当前过滤等级。
  */
LOG_LevelTypeDef LOG_GetLevel(void)
{
    return hlog_default.Level;
}

/**
  * @brief  获取默认日志实例的运行状态。
  * @retval LOG_StateTypeDef 当前运行状态。
  */
LOG_StateTypeDef LOG_GetState(void)
{
    return hlog_default.State;
}

/**
  * @brief  将已经装配完成的文本写入当前输出后端。
  * @param  MessageLevel 当前消息等级。
  * @param  Data         待输出文本缓冲区。
  * @param  Length       文本长度，不包含结尾的 '\0'。
  * @note   高于当前过滤等级的消息会被正常忽略并返回 LOG_OK。
  * @retval LOG_OK    输出成功，或消息被等级策略正常过滤。
  * @retval LOG_ERROR 参数、状态或底层输出接口无效。
  */
LOG_StatusTypeDef LOG_Write(LOG_LevelTypeDef MessageLevel,
                            const char *Data,
                            uint32_t Length)
{
    if ((hlog_default.State != LOG_STATE_READY) ||
        (Data == NULL) ||
        (Length == 0U) ||
        (LOG_IsMessageLevelValid(MessageLevel) == 0U))
    {
        return LOG_ERROR;
    }

    if (MessageLevel > hlog_default.Level)
    {
        return LOG_OK;
    }

    if ((hlog_default.Output.Ops == NULL) ||
        (hlog_default.Output.Ops->Write == NULL))
    {
        hlog_default.State = LOG_STATE_ERROR;
        return LOG_ERROR;
    }

    return hlog_default.Output.Ops->Write(hlog_default.Output.Context,
                                        Data,
                                        Length);
}

/**
  * @brief  装配并输出包含等级、时间戳和模块标签的日志。
  * @param  MessageLevel 当前消息等级。
  * @param  Tag          模块标签。
  * @param  Format       printf 风格的正文格式字符串。
  * @param  ...          Format 对应的可变参数。
  * @note   本函数使用固定大小的栈缓冲区；若完整日志超过
  *         LOG_FORMAT_BUFFER_SIZE，则返回 LOG_ERROR，不输出截断文本。
  * @retval LOG_OK    输出成功，或消息被等级策略正常过滤。
  * @retval LOG_ERROR 参数、状态、格式化或底层输出失败。
  */
    LOG_StatusTypeDef LOG_Printf(LOG_LevelTypeDef MessageLevel,
                                const char *Tag,
                                const char *Format,
                                ...)
{
#if APP_LOG_ENABLE == 0
    return LOG_ERROR;
#endif /* APP_LOG_ENABLE == 0 */

    char buffer[LOG_FORMAT_BUFFER_SIZE];
    uint32_t timestamp_ms;
    size_t used_length;
    size_t message_capacity;
    int result;
    va_list args;

    if ((hlog_default.State != LOG_STATE_READY) ||
        (Tag == NULL) ||
        (Format == NULL) ||
        (LOG_IsMessageLevelValid(MessageLevel) == 0U))
    {
        return LOG_ERROR;
    }

    if (MessageLevel > hlog_default.Level)
    {
        return LOG_OK;
    }

    if (hlog_default.TimeSource.GetTimeMs == NULL)
    {
        hlog_default.State = LOG_STATE_ERROR;
        return LOG_ERROR;
    }

    timestamp_ms = hlog_default.TimeSource.GetTimeMs(
        hlog_default.TimeSource.Context);

    result = snprintf(buffer,
                    sizeof(buffer),
                    "%c (%lu) %s: ",
                    LOG_LevelToChar(MessageLevel),
                    (unsigned long)timestamp_ms,
                    Tag);

    if ((result < 0) || ((size_t)result >= sizeof(buffer)))
    {
        return LOG_ERROR;
    }

    used_length = (size_t)result;

    if ((sizeof(buffer) - used_length) <= 2U)
    {
        return LOG_ERROR;
    }

    message_capacity = sizeof(buffer) - used_length - 2U;

    va_start(args, Format);
    result = vsnprintf(&buffer[used_length], message_capacity, Format, args);
    va_end(args);

    if ((result < 0) || ((size_t)result >= message_capacity))
    {
        return LOG_ERROR;
    }

    used_length += (size_t)result;
    buffer[used_length++] = '\r';
    buffer[used_length++] = '\n';
    buffer[used_length] = '\0';

    return LOG_Write(MessageLevel, buffer, (uint32_t)used_length);
}