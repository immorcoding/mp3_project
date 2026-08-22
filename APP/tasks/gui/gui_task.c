#include "gui_task.h"

#include "Service/gui/gui_service.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

void gui_task(void *handle)
{
    (void)handle;
    if (Service_GUI_Init() != SERVICE_OK)
    {
        Error_Handler();
    }

    while (1)
    {
        Service_GUI_Process();
        // vTaskDelay(pdMS_TO_TICKS(7));
    }
}
