/**
  ******************************************************************************
  * @file    log_task.c
  * @brief   FreeRTOS 日志消费任务实现。
  *
  * @details
  *          本任务是 LogService ready queue 的唯一消费者；它周期性推进日志核心的
  *          已绑定输出后端，并在每轮最多消费一条任务日志消息。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "APP/tasks/log/log_task.h"

#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/**
  * @brief  周期性推进日志输出并从 LogService 转交一条消息。
  * @param  handle 未使用，保留以满足 FreeRTOS TaskFunction_t。
  * @note   本任务不拥有日志消息块；LogService_Consume() 完成 ready queue 到
  *         Components/log 的转交，并负责归还消息块。5 ms 周期使 USB 暂忙时
  *         不会忙等，同时保证端口恢复后能尽快继续输出。
  */
void log_task(void *handle)
{
    (void)handle;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
        (void)LogService_Consume();
    }
}
