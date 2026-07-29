#include "log_service.h"

#include "Components/log/log.h"
#include "Components/log/log_config.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"

#include "stdio.h"
#include "string.h"

typedef struct
{
    LOG_LevelTypeDef level;
    const char *tag;    // 约定为字符串字面量，例如 "player"
    char text[64];     // 已格式化的正文
} LogServiceMessage_t;

static LogServiceMessage_t msg[LOGMSG_QUEUE_LENGTH];
static QueueHandle_t message_free_queue;
static QueueHandle_t message_ready_queue;
//零拷贝消息队列

LOG_StatusTypeDef Log_Service_Init(void)
{
    message_free_queue = xQueueCreate(LOGMSG_QUEUE_LENGTH, sizeof(LogServiceMessage_t *));
    message_ready_queue = xQueueCreate(LOGMSG_QUEUE_LENGTH, sizeof(LogServiceMessage_t *));

    if ((message_free_queue == NULL) || (message_ready_queue == NULL))
    {
        return LOG_ERROR;
    }

    memset(msg, 0, sizeof(msg));

    for(uint8_t i = 0; i < LOGMSG_QUEUE_LENGTH; i++)
    {
        LogServiceMessage_t *msg_addr = &msg[i];
        if (xQueueSend(message_free_queue, &msg_addr, 0) != pdPASS)
        {
            return LOG_ERROR;
        }
    }
    return LOG_OK;
}

LOG_StatusTypeDef LOG_Service_Post(LOG_LevelTypeDef Level, const char *tag, const char *text)
{
    if(Level > LOG_LEVEL_DEBUG || tag == NULL || text == NULL)
    {
        return LOG_ERROR;
    }

    LogServiceMessage_t *msg_addr; //接收一块空内存 转移到就绪队列
    if(xQueueReceive(message_free_queue, &msg_addr, 0) == pdFAIL)
    {
        return LOG_ERROR;
    }

    size_t len = strlen(text);

    if(len >= sizeof(msg_addr->text)) //字符串过长 留一个字节给\0防止越界
    {
        (void)xQueueSend(message_free_queue, &msg_addr, 0); //失败归还
        return LOG_ERROR;
    }

    //修改取出的空闲地址的属性
    msg_addr->level = Level;
    msg_addr->tag = tag;
    memcpy(msg_addr->text, text, len + 1); //留一个字节给\0防止越界

    if(xQueueSend(message_ready_queue, &msg_addr, 0) == pdFAIL)
    {
        (void)xQueueSend(message_free_queue, &msg_addr, 0); //失败归还
        return LOG_ERROR;
    }

    return LOG_OK;
}

LOG_StatusTypeDef LOG_Service_Consume(void)
{
    LOG_StatsTypeDef stats;
    LogServiceMessage_t *msg_addr;
    LOG_StatusTypeDef status;

    /*
     * 先尝试把 Components/log 环形队列中已有的一条日志提交给 USB。
     * USB 忙时 LOG_Process() 会保留队首，正常返回。
     */
    status = LOG_Process();

    if(status != LOG_OK)
    {
        return LOG_ERROR;
    }

    if (LOG_GetStats(&stats) != LOG_OK)
    {
        return LOG_ERROR;
    }

    if (stats.PendingCount >= LOG_QUEUE_DEPTH) //待发送消息塞满缓存池，返回
    {
        return LOG_OK;
    }

    if(xQueueReceive(message_ready_queue, &msg_addr, 0) == pdFAIL)
    {
        //没有待发送
        return LOG_OK;
    }

    status = LOG_Printf(msg_addr->level, msg_addr->tag, "%s", msg_addr->text);

    if(xQueueSend(message_free_queue, &msg_addr, 0) == pdFAIL)//归还
    {
        return LOG_ERROR;
    }

    return LOG_OK;
}