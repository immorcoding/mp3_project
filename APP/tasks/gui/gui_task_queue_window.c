/**
  ******************************************************************************
  * @file    gui_task_queue_window.c
  * @brief   GUI Task 私有的 listbuffer 窗口请求/消费状态机。
  ******************************************************************************
  */

#include "gui_task_queue_window.h"

/**
 * @brief 复位为「尚未要过窗」，下一窗从播放列表 0 起。
 * @param[out] client GUI Task 持有的窗口客户状态。
 */
void gui_task_queue_window_client_init(Gui_QueueWindowClientTypeDef *client)
{
    client->need_window = true;
    client->displayed = false;
    client->desired_index = 0U;
    client->applied_index = 0U;
    client->applied_length = 0U;
}

/**
 * @brief 标记上一窗已成功填进 Queue。
 * @param[in,out] client GUI Task 持有的窗口客户状态。
 * @param[in] index 本窗在播放列表上的起点，与 READY 槽位的 Index 一致。
 * @param[in] length 本窗实际条数，与 READY 槽位的 Length 一致。
 * @note 仅在 QueueApply 成功后调用。失败须保持 need_window，以便写回 IDLE 后重试。
 */
void gui_task_queue_window_mark_applied(
    Gui_QueueWindowClientTypeDef *client,
    uint16_t index,
    uint16_t length)
{
    client->need_window = false;
    client->displayed = true;
    client->applied_index = index;
    client->applied_length = length;
    client->desired_index = index;
}

/**
 * @brief 按已展示窗口与滚出顶部的整行数，计算下一窗起点。
 * @param[in] applied_index 已展示窗口在播放列表上的起点。
 * @param[in] applied_length 已展示窗口的实际条数。
 * @param[in] lead QueueTab 顶部已滚出的整行数。
 * @param[in] max_entries 窗口槽位上限；Length 不足此值表示已到列表末尾。
 * @return 应 request 的播放列表起点。Index=0 时不留上一窗；其余情况留一行给回滑。
 * @note 不把 listbuffer 改成环形数组；只移动窗口起点。
 */
uint16_t gui_task_queue_window_desired_index(
    uint16_t applied_index,
    uint16_t applied_length,
    uint16_t lead,
    uint16_t max_entries)
{
    uint32_t first_visible;
    uint16_t desired;

    first_visible = (uint32_t)applied_index + (uint32_t)lead;
    if (first_visible > 0xFFFFU)
    {
        first_visible = 0xFFFFU;
    }

    if (first_visible == 0U)
    {
        desired = 0U;
    }
    else
    {
        desired = (uint16_t)(first_visible - 1U);
    }

    if ((applied_length < max_entries) && (desired > applied_index))
    {
        desired = applied_index;
    }

    return desired;
}

/**
 * @brief 已展示窗口上根据滚动整行数更新下一窗起点；需要新窗时置 need_window。
 * @param[in,out] client GUI Task 持有的窗口客户状态。
 * @param[in] lead QueueTab 顶部已滚出的整行数。
 * @param[in] max_entries 窗口槽位上限。
 * @note 未展示时忽略，以免第一窗被滚动噪声改掉 Index=0。相等时不清除
 *       need_window，Apply 失败仍须靠原标记重试。手指滑动过程中即可改 Index，
 *       换窗只从当前 scroll_y 扣整行高度，不把列表吸回整页。
 */
void gui_task_queue_window_note_lead(
    Gui_QueueWindowClientTypeDef *client,
    uint16_t lead,
    uint16_t max_entries)
{
    uint16_t desired;

    if (!client->displayed)
    {
        return;
    }

    desired = gui_task_queue_window_desired_index(
        client->applied_index,
        client->applied_length,
        lead,
        max_entries);
    client->desired_index = desired;
    if (desired != client->applied_index)
    {
        client->need_window = true;
    }
}

/**
 * @brief 下一窗应 request 的播放列表起点。
 * @param[in] client GUI Task 持有的窗口客户状态。
 * @return `desired_index`；未滚动过时为 0。
 */
uint16_t gui_task_queue_window_request_index(
    const Gui_QueueWindowClientTypeDef *client)
{
    return client->desired_index;
}

/**
 * @brief 按 SD 就绪、是否仍挂载与窗口槽位状态决定本轮动作。
 * @param[in,out] client GUI Task 持有的窗口客户状态。
 * @param[in] sd_ready 可发起新 request（消抖中为假）。
 * @param[in] sd_mounted FatFs 卷仍挂载；消抖中只要未卸载仍为真。
 * @param[in] status 当前 `storage_listbuffer.Status`，取值须与 IDLE/PENDING/READY 宏一致。
 * @return 本轮应对窗口做的动作；READY 优先于卸载 CLEAR，避免槽位停在 READY。
 * @note 清空只看 sd_mounted，不把消抖当成拔卡。READY 不在此处清 need_window，
 *       须等 QueueApply 成功后再 mark_applied。拔卡把下一窗起点打回 0。
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
            client->desired_index = 0U;
            client->applied_index = 0U;
            client->applied_length = 0U;
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

/**
 * @brief 把 Storage 游标收成 QueueApply 可用的当前下标。
 * @param[in] cursor_valid `storage_playback_cursor_get` 是否成功。
 * @param[in] cursor_index 播放列表下标。
 * @param[in] cursor_generation 游标代次。
 * @param[in] window_generation 本窗 READY 载荷的代次。
 * @return 代次一致时的播放列表下标；否则 `GUI_TASK_QUEUE_WINDOW_NO_CURRENT`。
 * @note 不判断该下标是否落在本窗内；QueueApply 只高亮窗内匹配行。
 */
uint16_t gui_task_queue_window_current_index(
    bool cursor_valid,
    uint16_t cursor_index,
    uint32_t cursor_generation,
    uint32_t window_generation)
{
    if (!cursor_valid ||
        (cursor_generation == 0U) ||
        (cursor_generation != window_generation))
    {
        return GUI_TASK_QUEUE_WINDOW_NO_CURRENT;
    }

    return cursor_index;
}
