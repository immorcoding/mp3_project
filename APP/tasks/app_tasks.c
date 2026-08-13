/**
  ******************************************************************************
  * @file    app_tasks.c
  * @brief   FreeRTOS 应用任务创建和调度器启动。
  *
  * @details
  *          Create Task 先建立 Log Service 的静态消息池与 FreeRTOS 队列，再创建
  *          消费日志的 Log Task、独占 SD 热插拔普通上下文的 Storage Task、
  *          LCD 最小诊断 Task 以及监控 Task。所有任务创建成功后 Create Task 自删。
  ******************************************************************************
  */

#include "APP/tasks/app_tasks.h"

#include "APP/tasks/log/log_task.h"
#include "APP/tasks/lcd/lcd_task.h"
#include "APP/tasks/monitor/monitor_task.h"
#include "APP/tasks/storage/storage_task.h"
#include "Service/log/log_service.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/**
  * @brief  创建应用运行所需的全部任务，然后删除自身。
  * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
  * @note   LogService 必须先于任意可能调用 LogService_Post() 的任务创建。
  *         Storage Task 优先级高于 Log Task；它完成短暂初始化后会阻塞等待 SD
  *         检测通知，不会长期占用 CPU。LCD Task 当前只运行一次诊断事务，随后
  *         进入低频阻塞，因而与 Log/Monitor Task 同优先级即可。
  */
static void create_task(void *handle)
{
    (void)handle;

    if (LogService_Init() != LOG_OK)
    {
        Error_Handler();
    }

    if ((xTaskCreate(log_task,
                     "Log Task",
                     APP_LOG_TASK_STACK_WORDS,
                     NULL,
                     APP_LOG_TASK_PRIORITY,
                     NULL) != pdPASS) ||
        (xTaskCreate(storage_task,
                     "Storage Task",
                     APP_STORAGE_TASK_STACK_WORDS,
                     NULL,
                     APP_STORAGE_TASK_PRIORITY,
                     NULL) != pdPASS) ||
        (xTaskCreate(monitor_task,
                     "Monitor Task",
                     APP_MONITOR_TASK_STACK_WORDS,
                     NULL,
                     APP_MONITOR_TASK_PRIORITY,
                     NULL) != pdPASS) ||
        (xTaskCreate(lcd_task,
                     "LCD Task",
                     APP_LCD_TASK_STACK_WORDS,
                     NULL,
                     APP_LCD_TASK_PRIORITY,
                     NULL) != pdPASS))
    {
        Error_Handler();
    }

    vTaskDelete(NULL);
}

/**
  * @brief  创建 Create Task 并启动 FreeRTOS 调度器。
  * @note   调度器正常运行后不会返回。若返回，通常表示空闲任务创建失败或内核
  *         堆不足；此时直接进入 CubeMX 的 Error_Handler()。
  */
void app_task_start(void)
{
    if (xTaskCreate(create_task,
                    "Create Task",
                    APP_BOOT_TASK_STACK_WORDS,
                    NULL,
                    APP_BOOT_TASK_PRIORITY,
                    NULL) != pdPASS)
    {
        Error_Handler();
    }

    vTaskStartScheduler();
    Error_Handler();
}
