/**
  ******************************************************************************
  * @file    gui_task.h
  * @brief   GUI Task 私有入口。
  ******************************************************************************
  */

#ifndef GUI_TASK_H
#define GUI_TASK_H

/**
 * @brief GUI Task 私有的任务通知槽。
 * @note  索引只在本任务的通知数组内有效；与 Storage Task 的槽位编号互不相关。
 */
typedef enum
{
    GUI_NOTIFY_LCD_TRANSFER = 0U, /**< SPI DMA 刷新完成或错误，由 GUI Service 等待。 */
    GUI_NOTIFY_COUNT              /**< 本任务占用的通知槽数量，不是可等待的事件。 */
} Gui_NotifyIndexTypeDef;

void gui_task(void *handle);

#endif /* GUI_TASK_H */
