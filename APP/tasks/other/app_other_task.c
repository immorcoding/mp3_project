#include "app_other_task.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

void other_task(void *handle)
{
    (void)handle;
    while(1)
    {
        HAL_GPIO_TogglePin(USER_LED_GPIO_Port, USER_LED_Pin);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
