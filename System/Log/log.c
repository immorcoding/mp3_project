/**
  ******************************************************************************
  * @file    log.c
  * @brief   系统日志核心功能实现。
  *
  * @details
  *          本文件保存唯一的默认日志对象，完成等级过滤、时间戳读取、文本
  *          格式化、固定深度环形队列管理以及非阻塞端口调度。Handle 与端口
  *          绑定均不对应用层公开。
  *
  *          生产路径：LOG_Printf -> LOG_Write -> LOG_QueuePush。
  *          消费路径：LOG_Process -> Output.Ops->TryWrite -> LOG_QueuePop。
  *          两条路径通过 RAM 队列解耦，因此 USB 尚未打开时的启动日志仍可
  *          保留；当前实现不提供并发保护，只允许主循环单上下文调用。
  ******************************************************************************
  */
#include "APP/app_config.h"

/* Includes ------------------------------------------------------------------*/
#include "System/Log/log.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
    /* NONE 仅用于过滤配置，不能作为一条实际消息的等级。 */
    return ((Level != LOG_LEVEL_NONE) &&
            ((uint32_t)Level <= (uint32_t)LOG_LEVEL_DEBUG)) ? 1U : 0U;
}

/**
  * @brief  将一条完整日志复制到 RAM 队列。
  * @param  hlog   日志 Handle。
  * @param  Level  消息等级。
  * @param  Data   消息正文。
  * @param  Length 正文长度，不包含 '\0'。
  * @note   队列已满时丢弃最旧消息，优先保留最新故障现场。
  * @retval LOG_OK    入队成功。
  * @retval LOG_ERROR 参数或长度无效。
  */
static LOG_StatusTypeDef LOG_QueuePush(LOG_HandleTypeDef *hlog,
                                       LOG_LevelTypeDef Level,
                                       const char *Data,
                                       uint32_t Length)
{
    LOG_MessageTypeDef *message;

    if ((hlog == NULL) ||
        (Data == NULL) ||
        (Length == 0U) ||
        (Length >= LOG_FORMAT_BUFFER_SIZE))
    {
        return LOG_ERROR;
    }

    if (hlog->Queue.Count >= LOG_QUEUE_DEPTH)
    {
        /*
         * 队列已满时 Head 指向最旧消息。先移动 Head 并减少 Count，相当于
         * 丢弃最旧故障现场；随后仍把本次最新消息写入 Tail。
         */
        hlog->Queue.Head = (uint16_t)((hlog->Queue.Head + 1U) % LOG_QUEUE_DEPTH);
        hlog->Queue.Count--;
        hlog->Queue.DroppedCount++;
    }

    /*
     * 将调用者数据复制到队列自有槽位。复制完成后，调用者提供的缓冲区
     * 可以是局部变量，也可以立即被复用，不影响稍后的异步输出。
     */
    message = &hlog->Queue.Messages[hlog->Queue.Tail];
    (void)memcpy(message->Data, Data, Length);
    message->Data[Length] = '\0';
    message->Length = (uint16_t)Length;
    message->Level = Level;

    hlog->Queue.Tail = (uint16_t)((hlog->Queue.Tail + 1U) % LOG_QUEUE_DEPTH);
    hlog->Queue.Count++;
    hlog->Queue.EnqueuedCount++;

    return LOG_OK;
}

/**
  * @brief  移除当前队首日志。
  * @param  hlog 日志 Handle。
  * @retval None
  */
static void LOG_QueuePop(LOG_HandleTypeDef *hlog)
{
    if ((hlog == NULL) || (hlog->Queue.Count == 0U))
    {
        return;
    }

    hlog->Queue.Head = (uint16_t)((hlog->Queue.Head + 1U) % LOG_QUEUE_DEPTH);
    hlog->Queue.Count--;
}

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化默认日志实例。
  * @note   本函数设置默认过滤等级，并通过 LOG_Port_Bind() 绑定 USB 输出
  *         和毫秒时间源。重复调用会清空待发送队列、统计和端口快照。
  * @retval LOG_OK    初始化成功，日志状态变为 LOG_STATE_READY。
  * @retval LOG_ERROR 默认配置或端口接口无效，日志状态变为 LOG_STATE_ERROR。
  */
LOG_StatusTypeDef LOG_Init(void)
{
#if APP_LOG_ENABLE == 0
    return LOG_ERROR;
#endif /* APP_LOG_ENABLE == 0 */

    /* 一次性复位全部绑定、队列槽位、索引和统计计数器。 */
    (void)memset(&hlog_default, 0, sizeof(hlog_default));
    hlog_default.State = LOG_STATE_RESET;
    hlog_default.Level = LOG_DEFAULT_LEVEL;

    /* 编译期宏仍参与运行时校验，防止非法强制转换值进入 Handle。 */
    if ((uint32_t)hlog_default.Level > (uint32_t)LOG_LEVEL_DEBUG)
    {
        hlog_default.State = LOG_STATE_ERROR;
        return LOG_ERROR;
    }

    /* Port 统一装配 Output 与 TimeSource，应用层不能直接改写这些指针。 */
    if (LOG_Port_Bind(&hlog_default) != LOG_OK)
    {
        hlog_default.State = LOG_STATE_ERROR;
        return LOG_ERROR;
    }

    /* 对所有必需回调做最终防御性检查，避免稍后通过空函数指针调用。 */
    if ((hlog_default.Output.Ops == NULL) ||
        (hlog_default.Output.Ops->TryWrite == NULL) ||
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
  * @brief  非阻塞处理一条待发送日志。
  * @retval LOG_OK    队列为空、消息成功提交或输出暂时不可用。
  * @retval LOG_ERROR 日志未初始化或输出 Adapter 提交失败。
  */
LOG_StatusTypeDef LOG_Process(void)
{
    LOG_MessageTypeDef *message;
    LOG_OutputStatusTypeDef output_status;

    if ((hlog_default.State != LOG_STATE_READY) ||
        (hlog_default.Output.Ops == NULL) ||
        (hlog_default.Output.Ops->TryWrite == NULL))
    {
        return LOG_ERROR;
    }

    if (hlog_default.Queue.Count == 0U)
    {
        return LOG_OK;
    }

    /* 只查看队首，不提前出队；BUSY/NOT_READY 时必须保留原消息重试。 */
    message = &hlog_default.Queue.Messages[hlog_default.Queue.Head];
    output_status = hlog_default.Output.Ops->TryWrite(hlog_default.Output.Context,
                                                      message->Level,
                                                      message->Data,
                                                      message->Length);

    if (output_status == LOG_OUTPUT_OK)
    {
        /*
         * OK 表示端口接受了异步发送请求。USB 可能仍在实际传输，因此
         * SentCount 不是“PC 终端已经显示”的严格确认计数。
         */
        hlog_default.Queue.SentCount++;
        LOG_QueuePop(&hlog_default);
        return LOG_OK;
    }

    if ((output_status == LOG_OUTPUT_BUSY) ||
        (output_status == LOG_OUTPUT_NOT_READY))
    {
        /* 暂态不是错误：保持 Head/Count，等待下次主循环再次尝试。 */
        return LOG_OK;
    }

    /*
     * 永久错误若一直保留队首会阻塞后续所有消息，因此记录错误并丢弃
     * 当前消息，让队列仍有机会继续工作。
     */
    hlog_default.Queue.OutputErrorCount++;
    hlog_default.Queue.DroppedCount++;
    LOG_QueuePop(&hlog_default);
    return LOG_ERROR;
}

/**
  * @brief  获取日志队列及输出统计快照。
  * @param  Stats 接收统计数据的对象。
  * @retval LOG_OK    获取成功。
  * @retval LOG_ERROR 参数为空或日志尚未初始化。
  */
LOG_StatusTypeDef LOG_GetStats(LOG_StatsTypeDef *Stats)
{
    if ((Stats == NULL) || (hlog_default.State != LOG_STATE_READY))
    {
        return LOG_ERROR;
    }

    Stats->PendingCount = hlog_default.Queue.Count;
    Stats->EnqueuedCount = hlog_default.Queue.EnqueuedCount;
    Stats->SentCount = hlog_default.Queue.SentCount;
    Stats->DroppedCount = hlog_default.Queue.DroppedCount;
    Stats->OutputErrorCount = hlog_default.Queue.OutputErrorCount;

    return LOG_OK;
}

/**
  * @brief  将已经装配完成的文本复制到 RAM 日志队列。
  * @param  MessageLevel 当前消息等级。
  * @param  Data         待输出文本缓冲区。
  * @param  Length       文本长度，不包含结尾的 '\0'。
  * @note   高于当前过滤等级的消息会被正常忽略并返回 LOG_OK。
  * @retval LOG_OK    入队成功，或消息被等级策略正常过滤。
  * @retval LOG_ERROR 参数、状态或文本长度无效。
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
        /* 数值越大越详细；超过当前阈值的消息不占用队列。 */
        return LOG_OK;
    }

    return LOG_QueuePush(&hlog_default, MessageLevel, Data, Length);
}

/**
  * @brief  装配并入队一条包含等级、时间戳和模块标签的日志。
  * @param  MessageLevel 当前消息等级。
  * @param  Tag          模块标签。
  * @param  Format       printf 风格的正文格式字符串。
  * @param  ...          Format 对应的可变参数。
  * @note   本函数使用固定大小的栈缓冲区；若完整日志超过
  *         LOG_FORMAT_BUFFER_SIZE，则返回 LOG_ERROR，不输出截断文本。
  * @retval LOG_OK    入队成功，或消息被等级策略正常过滤。
  * @retval LOG_ERROR 参数、状态、时间源或格式化长度无效。
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

    /* 时间戳记录消息产生时刻，而不是稍后 USB 真正提交的时刻。 */
    timestamp_ms = hlog_default.TimeSource.GetTimeMs(hlog_default.TimeSource.Context);

    /* 先装配固定前缀，返回值是“不含 '\0' 的理论字符数”。 */
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

    /* 至少预留 CR、LF 和最终 '\0'；vsnprintf 的容量参数已包含 '\0'。 */
    if ((sizeof(buffer) - used_length) <= 2U)
    {
        return LOG_ERROR;
    }

    message_capacity = sizeof(buffer) - used_length - 2U;

    /*
     * message_capacity 已扣除 CR/LF 两字节。若 vsnprintf 报告需要的长度
     * 不小于容量，说明正文被截断；本模块选择整条拒绝而非发送半条日志。
     */
    va_start(args, Format);
    result = vsnprintf(&buffer[used_length], message_capacity, Format, args);
    va_end(args);

    if ((result < 0) || ((size_t)result >= message_capacity))
    {
        return LOG_ERROR;
    }

    /* 为串口终端统一追加 CRLF，再由 LOG_Write 复制到持久队列槽位。 */
    used_length += (size_t)result;
    buffer[used_length++] = '\r';
    buffer[used_length++] = '\n';
    buffer[used_length] = '\0';

    return LOG_Write(MessageLevel, buffer, (uint32_t)used_length);
}
