/**
  ******************************************************************************
  * @file    gui_task.c
  * @brief   GUI Task 的 FreeRTOS 运行循环。
  *
  * @details
  *          本任务是 LVGL 的唯一执行上下文。循环只做 Consume 输入、调用各
  *          产品分区 step、再 Process()。音乐窗口与 playing 在 music/。
  ******************************************************************************
  */

#include "gui_task.h"
#include "music/gui_music.h"

#include "Service/gui/gui_service.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/**
 * @brief  初始化 GUI Service 并持续推进唯一的 LVGL 执行上下文。
 * @param  handle 未使用；保留以符合 FreeRTOS TaskFunction_t 签名。
 * @note   GUI 初始化失败被当前产品定义为致命错误，任务会进入 Error_Handler()。
 *         正常路径不返回。各产品分区每圈 step 一次，不阻塞等待 READY。
 */
void gui_task(void *handle)
{
    (void)handle;

    _Static_assert((unsigned)GUI_NOTIFY_COUNT <=
                       (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
                   "GUI Task notify slots exceed FreeRTOS array length");

    if (Service_GUI_Init(GUI_NOTIFY_LCD_TRANSFER) != SERVICE_OK)
    {
        Error_Handler();
    }

    gui_music_init();

    while (1)
    {
        Service_GUI_InputTypeDef input;

        input.command = SERVICE_GUI_INPUT_NONE;
        input.sheet_index = 0U;
        (void)Service_GUI_ConsumeInput(&input);

        gui_music_step(&input);
        /* gui_books_step(&input); */
        /* gui_settings_step(&input); */

        Service_GUI_Process();
    }
}
