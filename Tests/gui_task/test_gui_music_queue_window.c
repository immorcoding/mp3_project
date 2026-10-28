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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

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
    gui_music_queue_window_mark_applied(&client, 0U, 8U, 1U);
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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U, 1U);
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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U, 1U);
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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U, 1U);
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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U, 1U);
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
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    action = gui_music_queue_window_poll(
        &client,
        false,
        false,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY);
}

/**
 * @brief 第一窗 Index=0；滚动未满回滑行数时不改起点。
 */
static void test_scroll_within_back_rows_keeps_index_zero(void)
{
    assert(gui_music_queue_window_desired_index(
               0U, 8U, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS, 8U) == 0U);
}

/**
 * @brief 滚过回滑行数再多一行，窗口起点跟上可见行，首行之上仍留回滑行。
 */
static void test_scroll_past_back_rows_requests_index_one(void)
{
    assert(gui_music_queue_window_desired_index(
               0U, 8U, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 1U, 8U) == 1U);
}

/**
 * @brief 窗口已含回滑行且可见首行正好在其下时保持 Index。
 */
static void test_stable_when_lead_matches_back_rows(void)
{
    assert(gui_music_queue_window_desired_index(
               6U, 8U, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS, 8U) == 6U);
}

/**
 * @brief 回到窗口顶部时一次退回整份回滑行（#157）：快速回滑每次换窗都补足首行之上的行，
 *        不是一窗只退一行。
 */
static void test_scroll_back_to_top_requests_back_rows_earlier(void)
{
    assert(gui_music_queue_window_desired_index(6U, 8U, 0U, 8U) ==
           (uint16_t)(6U - GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS));
}

/**
 * @brief 列表前部不足回滑行数时退到 Index=0，不越过列表起点。
 */
static void test_scroll_back_near_start_clamps_to_zero(void)
{
    assert(gui_music_queue_window_desired_index(1U, 8U, 0U, 8U) == 0U);
}

/**
 * @brief 末窗 Length 不足上限时，不得把 Index 再往列表外推。
 */
static void test_does_not_advance_index_when_window_is_short(void)
{
    assert(gui_music_queue_window_desired_index(
               5U, 3U, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 2U, 8U) == 5U);
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
    assert(gui_music_queue_window_desired_index(0U, 8U, 7U, 8U) ==
           (uint16_t)(7U - GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS));
}

/**
 * @brief 已展示窗口上 lead 要求新起点时，IDLE 下重新 request。
 */
static void test_note_lead_requests_when_desired_index_moves(void)
{
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U, 1U);
    gui_music_queue_window_note_lead(&client, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 1U, 8U);
    assert(gui_music_queue_window_request_index(&client) == 1U);
    action = gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(action == GUI_MUSIC_QUEUE_WINDOW_ACTION_REQUEST);
}

/**
 * @brief 第一窗内滚动未越过回滑行数时，不因为滚动就重新 request。
 */
static void test_note_lead_does_not_rerequest_within_back_rows(void)
{
    GUI_MusicQueueWindowClientTypeDef client;
    GUI_MusicQueueWindowActionTypeDef action;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 0U, 8U, 1U);
    gui_music_queue_window_note_lead(&client, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS, 8U);
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
    GUI_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    (void)gui_music_queue_window_poll(
        &client,
        true,
        true,
        GUI_MUSIC_QUEUE_WINDOW_READY);
    gui_music_queue_window_mark_applied(&client, 4U, 8U, 1U);
    (void)gui_music_queue_window_poll(
        &client,
        false,
        false,
        GUI_MUSIC_QUEUE_WINDOW_IDLE);
    assert(gui_music_queue_window_request_index(&client) == 0U);
}

/**
 * @brief APPLY 成功后记下已显示窗口的曲库代次；换窗更新，卸载 CLEAR 后回到 0（未显示）。
 */
static void test_apply_records_displayed_window_generation(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    assert(gui_music_queue_window_displayed_generation(&client) == 0U);

    gui_music_queue_window_mark_applied(&client, 0U, 8U, 7U);
    assert(gui_music_queue_window_displayed_generation(&client) == 7U);

    gui_music_queue_window_mark_applied(&client, 4U, 8U, 9U);
    assert(gui_music_queue_window_displayed_generation(&client) == 9U);

    assert(gui_music_queue_window_poll(
               &client,
               false,
               false,
               GUI_MUSIC_QUEUE_WINDOW_IDLE) == GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR);
    assert(gui_music_queue_window_displayed_generation(&client) == 0U);
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
    test_scroll_within_back_rows_keeps_index_zero();
    test_scroll_past_back_rows_requests_index_one();
    test_stable_when_lead_matches_back_rows();
    test_scroll_back_to_top_requests_back_rows_earlier();
    test_scroll_back_near_start_clamps_to_zero();
    test_does_not_advance_index_when_window_is_short();
    test_index_zero_has_no_previous_window();
    test_jump_scroll_requests_target_index_in_one_shot();
    test_note_lead_requests_when_desired_index_moves();
    test_note_lead_does_not_rerequest_within_back_rows();
    test_clear_resets_request_index_to_zero();
    test_apply_records_displayed_window_generation();
    puts("gui_music_queue_window_tests: all tests passed");
    return 0;
}
