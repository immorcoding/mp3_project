#include "gui_task.h"
#include "Service/gui/gui_service.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

void gui_task(void *handle)
{
    (void)handle;
    Service_GUI_Init();
    while (1)
    {
        // GUI task implementation
        Service_GUI_Process();
        vTaskDelay(pdMS_TO_TICKS(10)); // Add a small delay to allow other tasks to run
    }
}