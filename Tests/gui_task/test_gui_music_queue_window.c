/**
 ******************************************************************************
 * @file    test_gui_music_queue_window.c
 * @brief   GUI Task 窗口状态机的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "APP/tasks/gui/music/gui_music_queue_window.h"

/**
 * @brief 未挂载时不 request，也不清空本来就没有的 Queue。
 */
static void test_does_not_request_before_sd_ready(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    action = gui_music_queue_window_poll(
        &client,
        false,
        false,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief SD 就绪且槽位 IDLE 时要第一窗。
 */
static void test_requests_first_window_when_sd_ready_and_idle(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief PENDING 期间只等待，不重复 request。
 */
static void test_waits_while_pending(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_PENDING);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief READY 时整窗消费。
 */
static void test_applies_ready_window(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_PENDING);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY);
}

/**
 * @brief 消费成功后写回 IDLE 不得立刻再 request。
 */
static void test_does_not_rerequest_after_apply(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief Apply 失败未 mark 时，写回 IDLE 后继续 request。
 */
static void test_retries_request_if_apply_not_marked(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief request 失败槽位仍 IDLE 时，下一轮继续 request。
 */
static void test_retries_request_if_still_idle(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief 已展示窗口后卷卸载，清成空窗。
 */
static void test_clears_displayed_window_when_sd_removed(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U);
    action = gui_music_queue_window_poll(
        &client,
        false,
        false,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR);
}

/**
 * @brief 消抖离开就绪但卷仍挂载时，不清空已展示窗口。
 */
static void test_does_not_clear_during_debounce_while_mounted(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U);
    action = gui_music_queue_window_poll(
        &client,
        false,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief 拔卡后空窗只清一次。
 */
static void test_does_not_clear_twice_while_sd_absent(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U);
    (void)gui_music_queue_window_poll(
        &client,
        false,
        false,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    action = gui_music_queue_window_poll(
        &client,
        false,
        false,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief 重插卡后重新要窗。
 */
static void test_requests_again_after_sd_reinserted(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U);
    (void)gui_music_queue_window_poll(
        &client,
        false,
        false,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief 卸载时若槽位已是 READY，先 APPLY 消费，避免停在 READY。
 */
static void test_applies_ready_before_clear_when_sd_removed(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    action = gui_music_queue_window_poll(
        &client,
        false,
        false,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY);
}

/**
 * @brief 第一窗 Index=0；滚动未满一行上沿缓冲时不改起点。
 */
static void test_scroll_one_row_keeps_index_zero_for_upward_slack(void)
{
    assert(gui_music_queue_window_desired_index(0U, 8U, 1U, 8U) == 0U);
}

/**
 * @brief 滚过两行后窗口起点跟上可见行，并留一行给回滑。
 */
static void test_scroll_two_rows_requests_index_one(void)
{
    assert(gui_music_queue_window_desired_index(0U, 8U, 2U, 8U) == 1U);
}

/**
 * @brief 窗口已含一行上沿缓冲且仍停在该缓冲时保持 Index。
 */
static void test_stable_when_lead_matches_upward_slack(void)
{
    assert(gui_music_queue_window_desired_index(4U, 8U, 1U, 8U) == 4U);
}

/**
 * @brief 回到窗口顶部时 Index 减一，才能 request 上一窗。
 */
static void test_scroll_back_to_top_requests_previous_index(void)
{
    assert(gui_music_queue_window_desired_index(4U, 8U, 0U, 8U) == 3U);
}

/**
 * @brief 末窗 Length 不足上限时，不得把 Index 再往列表外推。
 */
static void test_does_not_advance_index_when_window_is_short(void)
{
    assert(gui_music_queue_window_desired_index(5U, 3U, 2U, 8U) == 5U);
}

/**
 * @brief 列表起点没有上一窗；lead 为 0 时保持 Index=0。
 */
static void test_index_zero_has_no_previous_window(void)
{
    assert(gui_music_queue_window_desired_index(0U, 8U, 0U, 8U) == 0U);
}

/**
 * @brief 一次滚过多行只 request 目标 Index，不逐步 +1。
 */
static void test_jump_scroll_requests_target_index_in_one_shot(void)
{
    assert(gui_music_queue_window_desired_index(0U, 8U, 5U, 8U) == 4U);
}

/**
 * @brief 已展示窗口上 lead 要求新起点时，IDLE 下重新 request。
 */
static void test_note_lead_requests_when_desired_index_moves(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U);
    gui_music_queue_window_note_lead(&client, 2U, 8U);
    assert(gui_music_queue_window_request_index(&client) == 1U);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief 上沿缓冲消耗完之前，不因为滚动就重新 request。
 */
static void test_note_lead_does_not_rerequest_for_slack_row(void)
{
    Gui_MusicQueueWindowClientTypeDef client;
    Gui_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U);
    gui_music_queue_window_note_lead(&client, 1U, 8U);
    assert(gui_music_queue_window_request_index(&client) == 0U);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
}

/**
 * @brief 拔卡清空后重插，仍从 Index=0 要第一窗。
 */
static void test_clear_resets_request_index_to_zero(void)
{
    Gui_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 4U, 8U);
    (void)gui_music_queue_window_poll(
        &client,
        false,
        false,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(gui_music_queue_window_request_index(&client) == 0U);
}

/**
 * @brief 游标有效且与窗口代次一致时，高亮用播放列表下标。
 */
static void test_current_index_follows_cursor_when_generation_matches(void)
{
    assert(gui_music_queue_window_current_index(true, 0U, 3U, 3U) == 0U);
    assert(gui_music_queue_window_current_index(true, 7U, 3U, 3U) == 7U);
}

/**
 * @brief 游标无效时 Queue 没有当前行。
 */
static void test_current_index_is_none_when_cursor_invalid(void)
{
    assert(gui_music_queue_window_current_index(false, 0U, 3U, 3U) ==
           GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 游标代次为 0 表示已作废。
 */
static void test_current_index_is_none_when_generation_is_zero(void)
{
    assert(gui_music_queue_window_current_index(true, 0U, 0U, 0U) ==
           GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 窗口代次与游标不一致时不得高亮旧下标。
 */
static void test_current_index_is_none_when_generation_mismatches(void)
{
    assert(gui_music_queue_window_current_index(true, 0U, 4U, 5U) ==
           GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 运行全部窗口状态机测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_does_not_request_before_sd_ready();
    test_requests_first_window_when_sd_ready_and_idle();
    test_waits_while_pending();
    test_applies_ready_window();
    test_does_not_rerequest_after_apply();
    test_retries_request_if_apply_not_marked();
    test_retries_request_if_still_idle();
    test_clears_displayed_window_when_sd_removed();
    test_does_not_clear_during_debounce_while_mounted();
    test_does_not_clear_twice_while_sd_absent();
    test_requests_again_after_sd_reinserted();
    test_applies_ready_before_clear_when_sd_removed();
    test_scroll_one_row_keeps_index_zero_for_upward_slack();
    test_scroll_two_rows_requests_index_one();
    test_stable_when_lead_matches_upward_slack();
    test_scroll_back_to_top_requests_previous_index();
    test_does_not_advance_index_when_window_is_short();
    test_index_zero_has_no_previous_window();
    test_jump_scroll_requests_target_index_in_one_shot();
    test_note_lead_requests_when_desired_index_moves();
    test_note_lead_does_not_rerequest_for_slack_row();
    test_clear_resets_request_index_to_zero();
    test_current_index_follows_cursor_when_generation_matches();
    test_current_index_is_none_when_cursor_invalid();
    test_current_index_is_none_when_generation_is_zero();
    test_current_index_is_none_when_generation_mismatches();

    puts("gui_music_queue_window_tests: all tests passed");
    return 0;
}
