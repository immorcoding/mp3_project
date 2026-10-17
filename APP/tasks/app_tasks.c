/**
  ******************************************************************************
  * @file    app_tasks.c
  * @brief   FreeRTOS 应用任务创建和调度器启动。
  *
  * @details
  *          Create Task 先建立 Log Service 的静态消息池与 FreeRTOS 队列，再创建
  *          消费日志的 Log Task、独占 SD 热插拔普通上下文的 Storage Task、
  *          持有播放状态机的 Playback Task（静态创建）、监控 Task 以及独占
  *          LVGL 执行上下文的 GUI Task。所有任务创建成功后 Create Task 自删。
  ******************************************************************************
  */

#include "APP/tasks/app_tasks.h"

#include "APP/tasks/log/log_task.h"
#include "APP/tasks/monitor/monitor_task.h"
#include "APP/tasks/playback/playback_task.h"
#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/gui/gui_task.h"

#include "Service/log/log_service.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/* Playback Task 的 TCB 与栈静态持有（A7），位于 DTCM .bss，不占内核堆。 */
static StaticTask_t playback_task_tcb;
static StackType_t playback_task_stack[APP_PLAYBACK_TASK_STACK_WORDS];

/**
  * @brief  创建应用运行所需的全部任务，然后删除自身。
  * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
  * @note   Service_Log 必须先于任意可能调用 Service_Log_Post() 的任务创建。
  *         Storage Task 优先级高于 Log Task；它完成短暂初始化后会阻塞等待 SD
  *         检测通知，不会长期占用 CPU。Playback Task 必须晚于 Storage Task 创建：
  *         创建时 Storage Task（优先级 2 高于本 Create Task）已运行到首次阻塞，
  *         已完成 storage_audio_stream_init()，播放任务才能调用 Storage 请求信箱；
  *         Playback Task 优先级 3 高于 Create Task，创建后立即运行到首次阻塞，
  *         完成命令 FIFO、状态信箱与回调注册，早于 GUI Task 发出第一条命令。
  *         它以 xTaskCreateStatic 创建，句柄不保留（任务经自身句柄接收通知）。
  *         GUI Task 持续推进 LVGL；SPI DMA 刷新时会由 GUI Service 的 wait callback
  *         阻塞等待传输完成。各任务的栈深度与优先级由 app_config.h 集中配置。
  */
static void create_task(void *handle)
{
    (void)handle;

    if (Service_Log_Init() != SERVICE_OK)
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
                     NULL) != pdPASS))
    {
        Error_Handler();
    }

    if (xTaskCreateStatic(playback_task,
                          "Playback Task",
                          APP_PLAYBACK_TASK_STACK_WORDS,
                          NULL,
                          APP_PLAYBACK_TASK_PRIORITY,
                          playback_task_stack,
                          &playback_task_tcb) == NULL)
    {
        Error_Handler();
    }

    if ((xTaskCreate(monitor_task,
                     "Monitor Task",
                     APP_MONITOR_TASK_STACK_WORDS,
                     NULL,
                     APP_MONITOR_TASK_PRIORITY,
                     NULL) != pdPASS) ||
        (xTaskCreate(gui_task,
                     "GUI Task",
                     APP_GUI_TASK_STACK_WORDS,
                     NULL,
                     APP_GUI_TASK_PRIORITY,
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
