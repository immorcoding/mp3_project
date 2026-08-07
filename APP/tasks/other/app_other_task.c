#include "app_other_task.h"
#include "APP/tasks/app_tasks.h"
#include "Components/log/log.h"
#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/portable.h"

#include "main.h"

#include "stdio.h"
#include "string.h"
#include <stdint.h>

UBaseType_t task_totalstack_get(const char *task_name)
{
    if (strcmp(task_name, "Create Task") == 0)
    {
        return CREATE_TASK_STACK_WORDS;
    }

    if (strcmp(task_name, "Storage Task") == 0)
    {
        return STORAGE_TASK_STACK_WORDS;
    }

    if (strcmp(task_name, "Log Task") == 0)
    {
        return LOG_TASK_STACK_WORDS;
    }

    if (strcmp(task_name, "Other Task") == 0)
    {
        return OTHER_TASK_STACK_WORDS;
    }

    if (strcmp(task_name, "IDLE") == 0)
    {
        return configMINIMAL_STACK_SIZE;
    }

#if (configUSE_TIMERS == 1)
    if (strcmp(task_name, "Tmr Svc") == 0)
    {
        return configTIMER_TASK_STACK_DEPTH;
    }
#endif

    return 0U;  /* 未登记的任务不输出占用率 */
}

void task_snapshot_get(void)
{
    UBaseType_t count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_snapshot;

    task_snapshot = pvPortMalloc(count * sizeof(TaskStatus_t));

    if(task_snapshot == NULL) return;

    count = uxTaskGetSystemState(task_snapshot,
                                             count,
                                             NULL);

    for(UBaseType_t i = 0; i < count; i++)
    {
        char post_str[64];

        UBaseType_t total_words = task_totalstack_get(task_snapshot[i].pcTaskName);
        UBaseType_t free_words = task_snapshot[i].usStackHighWaterMark;
        UBaseType_t used_words = total_words - free_words;
        unsigned long peak_x10 = (unsigned long)used_words * 1000 / (unsigned long)total_words; //snprintf似乎不能double

        (void)snprintf(post_str, 
                       64,
                       "%s: total= %lu B, min free = %lu B, peak = %lu.%lu%%",
                       task_snapshot[i].pcTaskName,
                       (unsigned long)(total_words * sizeof(StackType_t)),
                       (unsigned long)(free_words * sizeof(StackType_t)),
                       peak_x10 / 10,
                       peak_x10 % 10);

        LOG_Service_Post(LOG_LEVEL_INFO, "MONITOR", post_str);
    }

    vPortFree(task_snapshot);
}

void other_task(void *handle)
{
    (void)handle;
    uint16_t i = 0;
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(500));

        if((i++) % 10 == 0)
        {
            task_snapshot_get();
        }
        HAL_GPIO_TogglePin(USER_LED_GPIO_Port, USER_LED_Pin);
    }
}
