#include "Components/log/log.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "APP/tasks/log/app_log_task.h"
#include "APP/tasks/storage/app_storage_task.h"
#include "APP/tasks/other/app_other_task.h"

#include "main.h"


void create_task(void *handle) //创建所有任务后删除
{
    (void)handle;
    BaseType_t error_code;

    // error_code = xTaskCreate(storage_task, "Storage Task", (const uint16_t) 128, NULL, 4, NULL);
    error_code = xTaskCreate(log_task, "Log Task", (const uint16_t) 1024, NULL, 1, NULL);
    error_code = xTaskCreate(other_task, "Other Task", (const uint16_t) 128, NULL, 1, NULL);

    if(error_code == pdPASS)
    {
        (void)LOG_Printf(LOG_LEVEL_INFO,
                         "RTOS",
                         "All task created.");
        vTaskDelete(NULL); //删除自己
    }
    else
    {
        //日志任务都没启动，不发送日志
        Error_Handler();
    }
}

void app_tasks_init(void)
{
    BaseType_t error_code = xTaskCreate(create_task, "Create Task", (const uint16_t) 512, NULL, 0, NULL);

    if(error_code == pdFALSE)
    {
        //日志任务都没启动，不发送日志
        Error_Handler();
    }
    
    vTaskStartScheduler(); //调度器开启
    return;
}