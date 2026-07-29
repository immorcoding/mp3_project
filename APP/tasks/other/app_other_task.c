#include "app_other_task.h"
#include "main.h"

#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

void other_task(void *handle)
{
    (void)handle;
    while(1)
    {
        LOG_Service_Post(LOG_LEVEL_INFO, "LED", "LED Pin Toggled.");
        HAL_GPIO_TogglePin(USER_LED_GPIO_Port, USER_LED_Pin);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
