/**
  ******************************************************************************
  * @file    gui_task_queue_window.h
  * @brief   GUI Task 私有的 listbuffer 窗口请求/消费状态机。
  ******************************************************************************
  */

#ifndef GUI_TASK_QUEUE_WINDOW_H
#define GUI_TASK_QUEUE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#define GUI_TASK_QUEUE_WINDOW_IDLE    0U  /* 与 STORAGE_LISTBUFFER_IDLE 同值。 */
#define GUI_TASK_QUEUE_WINDOW_PENDING 1U  /* 与 STORAGE_LISTBUFFER_PENDING 同值。 */
#define GUI_TASK_QUEUE_WINDOW_READY   2U  /* 与 STORAGE_LISTBUFFER_READY 同值。 */
#define GUI_TASK_QUEUE_WINDOW_NO_CURRENT  0xFFFFU  /* 无有效游标或代次不符；与 QueueApply 无当前曲同值。 */

typedef enum
{
    GUI_TASK_QUEUE_WINDOW_ACTION_NONE = 0U, /**< 本轮不发起 request，也不填行。 */
    GUI_TASK_QUEUE_WINDOW_ACTION_REQUEST,   /**< SD 就绪且槽位 IDLE，应提交一窗。 */
    GUI_TASK_QUEUE_WINDOW_ACTION_APPLY,     /**< 槽位 READY，应整窗填行并写回 IDLE。 */
    GUI_TASK_QUEUE_WINDOW_ACTION_CLEAR      /**< 卷已卸载，应把 Queue 可见行清成空窗。 */
} Gui_QueueWindowActionTypeDef;

typedef struct
{
    bool need_window;        /**< 下一次 IDLE 且 SD 就绪时应 request。 */
    bool displayed;          /**< 当前 Queue 仍展示上一窗，卸载后需要 CLEAR。 */
    uint16_t desired_index;  /**< 下一窗在播放列表上的起点。 */
    uint16_t applied_index;  /**< 已成功填进 Queue 的窗口起点。 */
    uint16_t applied_length; /**< 已成功填进 Queue 的本窗条数。 */
} Gui_QueueWindowClientTypeDef;

void gui_task_queue_window_client_init(Gui_QueueWindowClientTypeDef *client);
void gui_task_queue_window_mark_applied(
    Gui_QueueWindowClientTypeDef *client,
    uint16_t index,
    uint16_t length);
uint16_t gui_task_queue_window_desired_index(
    uint16_t applied_index,
    uint16_t applied_length,
    uint16_t lead,
    uint16_t max_entries);
void gui_task_queue_window_note_lead(
    Gui_QueueWindowClientTypeDef *client,
    uint16_t lead,
    uint16_t max_entries);
uint16_t gui_task_queue_window_request_index(
    const Gui_QueueWindowClientTypeDef *client);
Gui_QueueWindowActionTypeDef gui_task_queue_window_poll(
    Gui_QueueWindowClientTypeDef *client,
    bool sd_ready,
    bool sd_mounted,
    uint32_t status);
uint16_t gui_task_queue_window_current_index(
    bool cursor_valid,
    uint16_t cursor_index,
    uint32_t cursor_generation,
    uint32_t window_generation);

#endif /* GUI_TASK_QUEUE_WINDOW_H */
