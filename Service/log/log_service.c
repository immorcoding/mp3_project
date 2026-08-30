/**
  ******************************************************************************
  * @file    log_service.c
  * @brief   FreeRTOS 异步日志消息块池 Implementation。
  *
  * @details
  *          静态消息块依次在 free queue、生产者局部变量和 ready queue 之间转移。
  *          两个 FreeRTOS 队列保存的是消息块指针值，不复制 128 B 的正文；Log task
  *          消费后把同一指针归还 free queue。所有公开函数只能在普通任务上下文调用。
  ******************************************************************************
  */

#include "Service/log/log_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "Components/log/log.h"
#include "Components/log/log_config.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"

/** @brief 单个静态日志块；tag 必须在 Log task 消费前保持有效。 */
typedef struct
{
    LOG_LevelTypeDef level;
    const char *tag;
    char text[128];
} log_service_message_t;

/** @brief 固定消息块池；其存储期覆盖所有生产者和 Log task。 */
static log_service_message_t message_pool[LOG_SERVICE_QUEUE_LENGTH];

/** @brief 可被生产者取得的空闲消息块地址队列。 */
static QueueHandle_t message_free_queue;

/** @brief 等待 Log task 转交至 Components/log 的消息块地址队列。 */
static QueueHandle_t message_ready_queue;

/**
  * @brief  将公开日志等级映射为日志核心的私有等级。
  * @param  service_level Service_Log 的调用参数。
  * @param  component_level 接收 Components/log 的对应等级。
  * @retval true 映射成功。
  * @retval false service_level 不是有效的公开枚举值，或输出参数无效。
  */
static bool log_service_make_component_level(
    Service_Log_LevelTypeDef service_level,
    LOG_LevelTypeDef *component_level)
{
    if (component_level == NULL)
    {
        return false;
    }

    switch (service_level)
    {
        case SERVICE_LOG_LEVEL_NONE:
            *component_level = LOG_LEVEL_NONE;
            return true;

        case SERVICE_LOG_LEVEL_ERROR:
            *component_level = LOG_LEVEL_ERROR;
            return true;

        case SERVICE_LOG_LEVEL_WARN:
            *component_level = LOG_LEVEL_WARN;
            return true;

        case SERVICE_LOG_LEVEL_INFO:
            *component_level = LOG_LEVEL_INFO;
            return true;

        case SERVICE_LOG_LEVEL_DEBUG:
            *component_level = LOG_LEVEL_DEBUG;
            return true;

        default:
            return false;
    }
}

/**
  * @brief  创建静态消息块池及 free/ready queue。
  * @retval SERVICE_OK 两个队列已创建，且每个消息块都位于 free queue。
  * @retval SERVICE_ERROR 队列创建或初始块入队失败。
  * @note   必须在任何调用 Service_Log_Post() 的任务开始运行前调用一次；不支持 ISR。
  */
Service_StatusTypeDef Service_Log_Init(void)
{
    message_free_queue = xQueueCreate(LOG_SERVICE_QUEUE_LENGTH,
                                      sizeof(log_service_message_t *));
    message_ready_queue = xQueueCreate(LOG_SERVICE_QUEUE_LENGTH,
                                       sizeof(log_service_message_t *));

    if ((message_free_queue == NULL) || (message_ready_queue == NULL))
    {
        return SERVICE_ERROR;
    }

    memset(message_pool, 0, sizeof(message_pool));

    for (uint8_t index = 0U; index < LOG_SERVICE_QUEUE_LENGTH; index++)
    {
        log_service_message_t *message = &message_pool[index];

        /* 队列复制的是 message 指针值，因此传入其地址供 xQueueSend() 读取。 */
        if (xQueueSend(message_free_queue, &message, 0U) != pdPASS)
        {
            return SERVICE_ERROR;
        }
    }

    return SERVICE_OK;
}

/**
  * @brief  将一条已格式化日志投递给 Log task。
  * @param  level 日志等级。
  * @param  tag 长期有效的标签字符串；通常应为字符串字面量。
  * @param  text 以 '\0' 结尾且长度不超过单块正文容量的日志内容。
  * @retval SERVICE_OK 消息块已从 free queue 移至 ready queue。
  * @retval SERVICE_INVALID_PARAM 参数无效或文本过长。
  * @retval SERVICE_NOT_READY 尚未调用 Service_Log_Init()。
  * @retval SERVICE_BUSY 当前没有空闲消息块。
  * @retval SERVICE_ERROR 队列转移失败。
 * @note   本函数从不等待空闲块；队列满时丢弃当前消息，避免业务任务因日志阻塞。
 *         时间戳由 Log task 稍后调用 LOG_Printf() 时生成，不能据此推断本函数的
 *         实际投递时刻。
  */
Service_StatusTypeDef Service_Log_Post(
    Service_Log_LevelTypeDef level,
    const char *tag,
    const char *text)
{
    LOG_LevelTypeDef component_level;
    log_service_message_t *message;
    size_t text_length;

    if ((tag == NULL) || (text == NULL) ||
        !log_service_make_component_level(level, &component_level))
    {
        return SERVICE_INVALID_PARAM;
    }

    if ((message_free_queue == NULL) || (message_ready_queue == NULL))
    {
        return SERVICE_NOT_READY;
    }

    if (xQueueReceive(message_free_queue, &message, 0U) != pdPASS)
    {
        return SERVICE_BUSY;
    }

    text_length = strlen(text);
    if (text_length >= sizeof(message->text))
    {
        /* 参数错误也必须把已取得的块归还，避免逐次投递耗尽整个消息池。 */
        (void)xQueueSend(message_free_queue, &message, 0U);
        return SERVICE_INVALID_PARAM;
    }

    message->level = component_level;
    message->tag = tag;
    memcpy(message->text, text, text_length + 1U);

    if (xQueueSend(message_ready_queue, &message, 0U) != pdPASS)
    {
        (void)xQueueSend(message_free_queue, &message, 0U);
        return SERVICE_ERROR;
    }

    return SERVICE_OK;
}

/**
  * @brief  推进底层日志输出，并在其有容量时转交一个 ready 消息块。
  * @retval SERVICE_OK 无待处理消息、输出暂忙，或已完成一次转交。
  * @retval SERVICE_NOT_READY 尚未调用 Service_Log_Init()。
  * @retval SERVICE_ERROR Components/log 处理、状态查询或消息块归还失败。
  * @note   仅由 Log task 调用。Components/log 的环形队列已满时不取 ready 消息，
  *         从而避免因队列满而丢弃 ready 消息。消息一旦转交给 LOG_Printf()，无论
  *         其输出结果如何，均视为本次消费完成并归还静态消息块。
  */
Service_StatusTypeDef Service_Log_Consume(void)
{
    LOG_StatsTypeDef stats;
    LOG_StatusTypeDef status;
    log_service_message_t *message;

    if ((message_free_queue == NULL) || (message_ready_queue == NULL))
    {
        return SERVICE_NOT_READY;
    }

    status = LOG_Process();
    if (status != LOG_OK)
    {
        return SERVICE_ERROR;
    }

    if (LOG_GetStats(&stats) != LOG_OK)
    {
        return SERVICE_ERROR;
    }

    if (stats.PendingCount >= LOG_QUEUE_DEPTH)
    {
        return SERVICE_OK;
    }

    if (xQueueReceive(message_ready_queue, &message, 0U) != pdPASS)
    {
        return SERVICE_OK;
    }

    (void)LOG_Printf(message->level, message->tag, "%s", message->text);

    /* 无论转交结果如何，消息块的本次所有权都在此结束，必须归还 free queue。 */
    if (xQueueSend(message_free_queue, &message, 0U) != pdPASS)
    {
        return SERVICE_ERROR;
    }

    return SERVICE_OK;
}
