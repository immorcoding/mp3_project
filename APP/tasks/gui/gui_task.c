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

#include <stddef.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/gui/gui_service.h"
#include "main.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/* 自启动以来单圈 Service_GUI_Process() 的最长周期数；仅 gui_task 写，其他任务只读。 */
static volatile uint32_t gui_task_max_frame_cycles;

/* spike（#191）：GUI 每圈结束后是否睡眠。默认睡；前台任务在基线阶段 P0 关掉以对比。 */
static volatile bool gui_task_sleep_enabled = true;

/* spike 观测计数：仅 gui_task 写，前台任务只读后做窗口差值。 */
static volatile uint32_t gui_task_loop_count;
static volatile uint32_t gui_task_sleep_ms_total;

/* 窗口内最长一圈：读者置 reset 请求，gui_task 在下一圈开头自己清零，保持单写者。 */
static volatile uint32_t gui_task_window_max_frame_cycles;
static volatile bool gui_task_window_reset_request;

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
 * @brief  打开或关闭 GUI 每圈结束后的睡眠（spike 用，#191）。
 * @param[in] enabled false 时 GUI 不睡，等同 0.9.0 的连续循环，用于基线对比。
 * @note   任意任务可调用；单字节写入，GUI Task 下一圈生效。
 */
void gui_task_set_sleep_enabled(bool enabled)
{
    gui_task_sleep_enabled = enabled;
}

/**
 * @brief  查询 GUI 睡眠开关。
 * @return true 表示每圈结束后按 Service_GUI_Process() 的返回值睡眠。
 */
bool gui_task_get_sleep_enabled(void)
{
    return gui_task_sleep_enabled;
}

/**
 * @brief  拷出 GUI 运行计数快照。
 * @param[out] stats 接收快照；为空时什么也不做。
 * @note   各字段单独读取，彼此之间不保证同一时刻（窗口差值对 spike 足够）。
 */
void gui_task_get_stats(GUI_TaskStatsTypeDef *stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->loops = gui_task_loop_count;
    stats->sleep_ms_total = gui_task_sleep_ms_total;
    stats->flush_count = Service_GUI_GetFlushCount();
    stats->window_max_frame_cycles = gui_task_window_max_frame_cycles;
}

/**
 * @brief  请求清零窗口内最长一圈；GUI Task 在下一圈开头执行。
 */
void gui_task_stats_window_reset(void)
{
    gui_task_window_reset_request = true;
}

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
        uint32_t wait_ms;

        if (gui_task_window_reset_request)
        {
            gui_task_window_max_frame_cycles = 0U;
            gui_task_window_reset_request = false;
        }

        input.command = SERVICE_GUI_INPUT_NONE;
        input.param = 0U;
        (void)Service_GUI_ConsumeInput(&input);

        gui_music_step(&input);
        /* gui_books_step(&input); */
        /* gui_settings_step(&input); */

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
            if (elapsed_cycles > gui_task_window_max_frame_cycles)
            {
                gui_task_window_max_frame_cycles = elapsed_cycles;
            }
        }

        gui_task_loop_count++;

        /* 睡眠在 DWT 测量段之外（#190）：不改变 gui_task_get_max_frame_cycles() 的口径。 */
        if (gui_task_sleep_enabled)
        {
            gui_task_sleep_ms_total += wait_ms;
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
        }
    }
}
