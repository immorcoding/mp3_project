#include "app_log_task.h"

#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

void log_task(void *handle)
{
    (void)handle;
    Log_Service_Init();

    while(1)
    {
        (void)LOG_Service_Consume();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
