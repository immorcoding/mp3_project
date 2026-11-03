/**
  ******************************************************************************
  * @file    gui_task.h
  * @brief   GUI Task 私有入口。
  ******************************************************************************
  */

#ifndef GUI_TASK_H
#define GUI_TASK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief GUI Task 私有的任务通知槽。
 * @note  索引只在本任务的通知数组内有效；与 Storage Task 的槽位编号互不相关。
 */
typedef enum
{
    GUI_NOTIFY_LCD_TRANSFER = 0U, /**< SPI DMA 刷新完成或错误，由 GUI Service 等待。 */
    GUI_NOTIFY_COUNT              /**< 本任务占用的通知槽数量，不是可等待的事件。 */
} GUI_NotifyIndexTypeDef;

/**
 * @brief GUI Task 的运行计数快照（spike 观测用，#191）。
 * @note  `loops`、`sleep_ms_total`、`flush_count` 自启动累计，读者自己做窗口差值；
 *        `window_max_frame_cycles` 是自上次 `gui_task_stats_window_reset()` 以来的最长一圈。
 */
typedef struct
{
    uint32_t loops;                   /**< 主循环圈数。 */
    uint32_t sleep_ms_total;          /**< 实际执行的睡眠毫秒数累计（不睡时不累加）。 */
    uint32_t flush_count;             /**< LVGL 向 Platform LCD 发起 flush 的次数。 */
    uint32_t window_max_frame_cycles; /**< 窗口内单圈 Service_GUI_Process() 最长核心周期数。 */
} GUI_TaskStatsTypeDef;

void gui_task(void *handle);
uint32_t gui_task_get_max_frame_cycles(void);
void gui_task_set_sleep_enabled(bool enabled);
bool gui_task_get_sleep_enabled(void);
void gui_task_get_stats(GUI_TaskStatsTypeDef *stats);
void gui_task_stats_window_reset(void);

#endif /* GUI_TASK_H */
