/**
  ******************************************************************************
  * @file    gui_music_queue_window.c
  * @brief   音乐分区私有的 Queue 窗口客户：写最新窗口需求、套用前台任务发布的窗口、空窗时清空 Queue。
  ******************************************************************************
  */

#include "gui_music_queue_window.h"

/**
 * @brief 复位为「尚未要过窗」，下一窗从播放列表 0 起，还没往需求格写过。
 * @param[out] client 音乐分区持有的窗口客户状态。
 */
void gui_music_queue_window_client_init(GUI_MusicQueueWindowClientTypeDef *client)
{
    client->need_window = true;
    client->displayed = false;
    client->desired_index = 0U;
    client->applied_index = 0U;
    client->applied_length = 0U;
    client->applied_generation = 0U;
    client->written = false;
    client->written_index = 0U;
    client->apply_retry = false;
}

/**
 * @brief 标记上一窗已成功填进 Queue。
 * @param[in,out] client 音乐分区持有的窗口客户状态。
 * @param[in] index 本窗在播放列表上的起点，与窗口结果的 Index 一致。
 * @param[in] length 本窗实际条数，与窗口结果的 Length 一致。
 * @param[in] generation 本窗的曲库代次，与窗口结果的 Generation 一致（#72 C3）。
 * @note 仅在 QueueApply 成功后调用。失败调用 `gui_music_queue_window_apply_failed()`，下一帧从 GUI 的窗口副本重试。
 */
void gui_music_queue_window_mark_applied(
    GUI_MusicQueueWindowClientTypeDef *client,
    uint16_t index,
    uint16_t length,
    uint32_t generation)
{
    client->need_window = false;
    client->displayed = true;
    client->apply_retry = false;
    client->applied_index = index;
    client->applied_length = length;
    client->applied_generation = generation;
    client->desired_index = index;
}

/**
 * @brief 已显示窗口的曲库代次，供当前曲高亮与窗口对账。
 * @param[in] client 音乐分区持有的窗口客户状态。
 * @return APPLY 时记下的代次；尚未显示或已被卸载清空时为 0。
 */
uint32_t gui_music_queue_window_displayed_generation(
    const GUI_MusicQueueWindowClientTypeDef *client)
{
    return client->displayed ? client->applied_generation : 0U;
}

/**
 * @brief 按已展示窗口与滚出顶部的整行数，计算下一窗起点。
 * @param[in] applied_index 已展示窗口在播放列表上的起点。
 * @param[in] applied_length 已展示窗口的实际条数。
 * @param[in] lead QueueTab 顶部已滚出的整行数。
 * @param[in] max_entries 窗口行数上限；Length 不足此值表示已到列表末尾。
 * @return 应要的播放列表起点。可见首行之上留 GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS 行给回滑，
 *         列表前部不足时为 0。
 * @note 不把窗口做成环形数组；只移动窗口起点。回滑余量是多行而不是一行（#157）：
 *       快速回滑时手指在下一窗到达之前就用完一行余量；回到窗口顶部时一次退回整份余量。
 */
uint16_t gui_music_queue_window_desired_index(
    uint16_t applied_index,
    uint16_t applied_length,
    uint16_t lead,
    uint16_t max_entries)
{
    uint32_t first_visible;
    uint16_t desired;

    first_visible = (uint32_t)applied_index + (uint32_t)lead;
    if (first_visible > (uint32_t)UINT16_MAX)
    {
        first_visible = (uint32_t)UINT16_MAX;
    }

    if (first_visible < GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS)
    {
        desired = 0U;
    }
    else
    {
        desired = (uint16_t)(first_visible - GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS);
    }

    if ((applied_length < max_entries) && (desired > applied_index))
    {
        desired = applied_index;
    }

    return desired;
}

/**
 * @brief 已展示窗口上根据滚动整行数更新下一窗起点；需要新窗时置 need_window。
 * @param[in,out] client 音乐分区持有的窗口客户状态。
 * @param[in] lead QueueTab 顶部已滚出的整行数。
 * @param[in] max_entries 窗口行数上限。
 * @note 未展示时忽略，以免第一窗被滚动噪声改掉 Index=0。相等时不清除
 *       need_window，Apply 失败仍须靠原标记重试。手指滑动过程中即可改 Index，
 *       换窗只从当前 scroll_y 扣整行高度，不把列表吸回整页。
 */
void gui_music_queue_window_note_lead(
    GUI_MusicQueueWindowClientTypeDef *client,
    uint16_t lead,
    uint16_t max_entries)
{
    uint16_t desired;

    if (!client->displayed)
    {
        return;
    }

    desired = gui_music_queue_window_desired_index(
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
 * @brief 下一窗应要的播放列表起点。
 * @param[in] client 音乐分区持有的窗口客户状态。
 * @return `desired_index`；未滚动过时为 0。
 */
uint16_t gui_music_queue_window_request_index(
    const GUI_MusicQueueWindowClientTypeDef *client)
{
    return client->desired_index;
}

/**
 * @brief 本帧是否该往窗口需求格写（或重写）需求。
 * @param[in] client 音乐分区持有的窗口客户状态。
 * @return 最近写出的起点不是现在想要的起点（或还没写过）时为真。写失败（需求格拿不到锁）后保持为真，
 *         直到 `gui_music_queue_window_request_sent()`。
 * @note  需求是状态：格子里始终是“GUI 现在想要的窗口起点”。滚动途中起点又变，直接写新的起点，不等上一份结果回来
 *        （新的覆盖旧的）；起点又回到已显示的窗口时同样要写回去，撤销上一个还没答的需求，否则前台会再发一份用户已经
 *        不需要的窗口。起点没变不重写，因为前台任务会按最新需求主动重新发窗（曲库发布后、卷就绪后），GUI 不用重复要。
 */
bool gui_music_queue_window_request_due(const GUI_MusicQueueWindowClientTypeDef *client)
{
    return !client->written || (client->written_index != client->desired_index);
}

/**
 * @brief 需求已写进窗口需求格：记下写出的起点。
 * @param[in,out] client 音乐分区持有的窗口客户状态。
 */
void gui_music_queue_window_request_sent(GUI_MusicQueueWindowClientTypeDef *client)
{
    client->written = true;
    client->written_index = client->desired_index;
}

/**
 * @brief QueueApply 失败：下一帧从 GUI 的窗口副本重试，不重新要。
 * @param[in,out] client 音乐分区持有的窗口客户状态。
 */
void gui_music_queue_window_apply_failed(GUI_MusicQueueWindowClientTypeDef *client)
{
    client->apply_retry = true;
}

/**
 * @brief 按本帧是否读到新窗口结果及其代次决定动作。
 * @param[in,out] client 音乐分区持有的窗口客户状态。
 * @param[in] new_result 本帧从结果格读到了新的一份窗口（已发布过的）。
 * @param[in] result_generation 新窗口的曲库代次；0 表示空窗（曲库不可用、代次变了、卷没了）。
 * @return 本帧应对窗口做的动作：新窗口或上次套用失败的重试返回 APPLY；空窗且 Queue 正显示着一窗返回 CLEAR（一次）。
 * @note  窗口不比需求序号，晚到的旧窗口也照常套用（#200）。需求没有因为套用失败而丢：need_window 要等 `mark_applied()` 才清。
 *        空窗之后把下一窗起点打回 0，仍需要一窗：已写出的起点不是 0 时 `request_due()` 为真，重新要第一窗；
 *        起点本来就是 0 时不用重写，曲库发布后前台任务按最新需求重新发窗。本来就没有显示窗口时收到空窗（开机曲库没好）
 *        不清空本来就没有的 Queue。
 */
GUI_MusicQueueWindowActionTypeDef gui_music_queue_window_poll(
    GUI_MusicQueueWindowClientTypeDef *client,
    bool new_result,
    uint32_t result_generation)
{
    if (new_result && (result_generation == 0U))
    {
        const bool was_displayed = client->displayed;

        client->displayed = false;
        client->need_window = true;
        client->desired_index = 0U;
        client->applied_index = 0U;
        client->applied_length = 0U;
        client->applied_generation = 0U;
        client->apply_retry = false;
        return was_displayed ? GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR : GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE;
    }

    if (new_result || client->apply_retry)
    {
        client->apply_retry = false;
        return GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY;
    }

    return GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE;
}
