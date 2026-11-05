/**
  ******************************************************************************
  * @file    gui_task.c
  * @brief   GUI Task 的 FreeRTOS 运行循环。
  *
  * @details
  *          本任务是 LVGL 的唯一执行上下文。循环只做 Consume 输入、遍历应用注册表
  *          调用各应用的 gui_step、再 Process()，最后按 Process() 的返回值睡一段
  *          （GUI-28）。音乐窗口与 playing 在 APP/apps/music/gui/，本文件不认识任何具体应用。
  ******************************************************************************
  */

#include "gui_task.h"
#include "APP/apps/app_table.h"

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/gui/gui_service.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/* 自启动以来单圈 Service_GUI_Process() 的最长周期数；仅 gui_task 写，其他任务只读。 */
static volatile uint32_t gui_task_max_frame_cycles;

/**
 * @brief  读取 GUI 最长一帧的核心周期数（只读查询）。
 * @retval 启动以来 Service_GUI_Process() 单圈耗时最大值，单位为核心周期；尚无样本为 0。
 * @note   任意任务可调用。32 位对齐读写在 Cortex-M7 上不撕裂，唯一写者是 GUI Task。
 */
uint32_t gui_task_get_max_frame_cycles(void)
{
    return gui_task_max_frame_cycles;
}

/**
 * @brief  初始化 GUI Service 并持续推进唯一的 LVGL 执行上下文。
 * @param  handle 未使用；保留以符合 FreeRTOS TaskFunction_t 签名。
 * @note   GUI 初始化失败被当前产品定义为致命错误，任务会进入 Error_Handler()。
 *         正常路径不返回。注册表里各应用的 gui_step 每圈调用一次，不阻塞等待 READY。
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

    for (size_t i = 0U; i < app_table_count; i++)
    {
        if (app_table[i].gui_init != NULL)
        {
            app_table[i].gui_init();
        }
    }

    while (1)
    {
        Service_GUI_InputTypeDef input;
        uint32_t wait_ms;

        input.command = SERVICE_GUI_INPUT_NONE;
        input.param = 0U;
        (void)Service_GUI_ConsumeInput(&input);

        for (size_t i = 0U; i < app_table_count; i++)
        {
            if (app_table[i].gui_step != NULL)
            {
                app_table[i].gui_step(&input);
            }
        }

        {
            /* 计数器由 app_init() 在 Resource 加载前启动；这里只读，不复位，避免干扰其他测量。 */
            const uint32_t start_cycles = CortexM7CycleCounter_Read();
            uint32_t elapsed_cycles;

            wait_ms = Service_GUI_Process();
            elapsed_cycles = CortexM7CycleCounter_Read() - start_cycles;
            if (elapsed_cycles > gui_task_max_frame_cycles)
            {
                gui_task_max_frame_cycles = elapsed_cycles;
            }
        }

        /*
         * 睡眠必须在上面的测量段之外，最长帧只含渲染，不含睡眠（WF-12）。
         * 用 vTaskDelay 而不是带超时的通知等待：新结果到达不提前唤醒，GUI 不晚于下一次
         * 刷新醒来，且先跑 gui_step 再刷新，到期前到达的结果都赶得上同一帧（GUI-28）。
         */
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}
