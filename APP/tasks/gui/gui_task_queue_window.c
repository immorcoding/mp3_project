/**
  ******************************************************************************
  * @file    gui_task_queue_window.c
  * @brief   GUI Task 私有的 listbuffer 窗口请求/消费状态机。
  ******************************************************************************
  */

#include "gui_task_queue_window.h"

/**
 * @brief 复位为「尚未要过窗」。
 * @param[out] client GUI Task 持有的窗口客户状态。
 */
void gui_task_queue_window_client_init(Gui_QueueWindowClientTypeDef *client)
{
    client->need_window = true;
    client->displayed = false;
}

/**
 * @brief 标记上一窗已成功填进 Queue。
 * @param[in,out] client GUI Task 持有的窗口客户状态。
 * @note 仅在 QueueApply 成功后调用。失败须保持 need_window，以便写回 IDLE 后重试。
 */
void gui_task_queue_window_mark_applied(Gui_QueueWindowClientTypeDef *client)
{
    client->need_window = false;
    client->displayed = true;
}

/**
 * @brief 按 SD 就绪、是否仍挂载与窗口槽位状态决定本轮动作。
 * @param[in,out] client GUI Task 持有的窗口客户状态。
 * @param[in] sd_ready 可发起新 request（消抖中为假）。
 * @param[in] sd_mounted FatFs 卷仍挂载；消抖中只要未卸载仍为真。
 * @param[in] status 当前 `storage_listbuffer.Status`，取值须与 IDLE/PENDING/READY 宏一致。
 * @return 本轮应对窗口做的动作；READY 优先于卸载 CLEAR，避免槽位停在 READY。
 * @note 清空只看 sd_mounted，不把消抖当成拔卡。READY 不在此处清 need_window，
 *       须等 QueueApply 成功后再 mark_applied。
 */
Gui_QueueWindowActionTypeDef gui_task_queue_window_poll(
    Gui_QueueWindowClientTypeDef *client,
    bool sd_ready,
    bool sd_mounted,
    uint32_t status)
{
    if (status == GUI_TASK_QUEUE_WINDOW_READY)
    {
        return GUI_TASK_QUEUE_WINDOW_ACTION_APPLY;
    }

    if (!sd_mounted)
    {
        if (client->displayed)
        {
            client->displayed = false;
            client->need_window = true;
            return GUI_TASK_QUEUE_WINDOW_ACTION_CLEAR;
        }

        return GUI_TASK_QUEUE_WINDOW_ACTION_NONE;
    }

    if ((status == GUI_TASK_QUEUE_WINDOW_IDLE) && sd_ready && client->need_window)
    {
        return GUI_TASK_QUEUE_WINDOW_ACTION_REQUEST;
    }

    return GUI_TASK_QUEUE_WINDOW_ACTION_NONE;
}
