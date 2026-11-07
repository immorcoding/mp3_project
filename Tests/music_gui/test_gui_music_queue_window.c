/**
 ******************************************************************************
 * @file    test_gui_music_queue_window.c
 * @brief   GUI 音乐分区窗口客户的主机行为测试（GUI-9、GUI-16、STOR-6、#193、#218）：按格子改写。
 *
 * @details
 *          窗口客户不再有 IDLE / PENDING / READY 三态，也不查询卷状态：GUI 把最新的窗口起点写进需求格（只留起点），
 *          前台任务整份发布窗口到结果格，客户读到新结果就套用（窗口不比序号，自带起点、代次和条数），代次 0 的空窗就清空 Queue。
 *          套用失败从 GUI 自己的窗口副本重试，不用重新要。起点计算（回滑行、末窗不越界）与原来相同。
 *          本测试扮演 GUI 任务：每帧 `note_lead`、`poll`，到点“写需求”（`request_due` → `request_index` → `request_sent`）。
 *          期望值写成字面量；只经 `gui_music_queue_window.h` 的公开函数观察。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "APP/apps/music/gui/gui_music_queue_window.h"

/**
 * @brief 把客户推进到“已显示以 index 起、length 条、代次 generation 的窗口”，并且需求格里写的就是这个起点（现实里 GUI 先写需求、
 *        前台答了、GUI 才套用）。
 * @param[out] client 窗口客户。
 * @param[in] index 窗口起点。
 * @param[in] length 条数。
 * @param[in] generation 代次。
 */
static void shown_window(GUI_MusicQueueWindowClientTypeDef *client, uint16_t index, uint16_t length, uint32_t generation)
{
    gui_music_queue_window_client_init(client);
    gui_music_queue_window_mark_applied(client, index, length, generation);
    gui_music_queue_window_request_sent(client); /* 需求格里就是已显示窗口的起点。 */
}

/**
 * @brief 像 gui_music.c 那样写需求：到点就取起点、记为已写出。
 * @param[in,out] client 窗口客户。
 * @return 写出的起点；本帧没有写为 -1。
 */
static int32_t write_if_due(GUI_MusicQueueWindowClientTypeDef *client)
{
    int32_t written = -1;

    if (gui_music_queue_window_request_due(client))
    {
        written = (int32_t)gui_music_queue_window_request_index(client);
        gui_music_queue_window_request_sent(client);
    }
    return written;
}

/** @brief 开机第一帧就要第一窗：需求到点，起点是 0；写出后不再重复写。 */
static void test_writes_the_first_window_request_right_away(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    assert(gui_music_queue_window_poll(&client, false, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
    assert(write_if_due(&client) == 0);
    assert(write_if_due(&client) == -1);
    assert(write_if_due(&client) == -1);
}

/** @brief 需求格拿不到锁（写失败）：保持“到点”，下一帧用同一个起点重试，直到写成功。 */
static void test_a_failed_write_is_retried_on_the_next_frame(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    assert(gui_music_queue_window_request_due(&client));
    assert(gui_music_queue_window_request_index(&client) == 0U);
    /* 写失败：不调 request_sent。 */
    assert(gui_music_queue_window_request_due(&client));
    assert(gui_music_queue_window_request_index(&client) == 0U);
    assert(write_if_due(&client) == 0);
    assert(!gui_music_queue_window_request_due(&client));
}

/** @brief 读到新窗口就套用；套用成功记下已显示窗口，不再重新要。 */
static void test_applies_a_new_window_and_does_not_ask_again(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    (void)write_if_due(&client);
    assert(gui_music_queue_window_poll(&client, true, 3U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY);
    gui_music_queue_window_mark_applied(&client, 0U, 12U, 3U);

    assert(gui_music_queue_window_poll(&client, false, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
    assert(write_if_due(&client) == -1);
    assert(gui_music_queue_window_displayed_generation(&client) == 3U);
}

/** @brief 窗口不比序号：不是最近一次要的起点的窗口（晚到的旧窗口）也照常套用，与今天一样（#200）。 */
static void test_a_window_for_an_older_request_is_applied_like_any_other(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    shown_window(&client, 0U, 12U, 3U);
    gui_music_queue_window_note_lead(&client, 9U, 12U);
    assert(write_if_due(&client) == 6); /* 起点 6 写出。 */

    assert(gui_music_queue_window_poll(&client, true, 3U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY); /* 起点 3 的旧窗口晚到。 */
}

/** @brief 套用失败从 GUI 的窗口副本重试：不重新写需求，也不丢掉“还需要这一窗”。 */
static void test_a_failed_apply_is_retried_from_the_gui_copy_without_asking_again(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    (void)write_if_due(&client);
    assert(gui_music_queue_window_poll(&client, true, 3U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY);
    gui_music_queue_window_apply_failed(&client);

    assert(gui_music_queue_window_poll(&client, false, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY);
    assert(write_if_due(&client) == -1);
    assert(gui_music_queue_window_displayed_generation(&client) == 0U); /* 还没有成功显示。 */

    gui_music_queue_window_mark_applied(&client, 0U, 12U, 3U);
    assert(gui_music_queue_window_poll(&client, false, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
}

/** @brief 套用失败重试时又来了新窗口：以新窗口为准，只套用一次。 */
static void test_a_newer_window_replaces_a_failed_apply(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    (void)write_if_due(&client);
    assert(gui_music_queue_window_poll(&client, true, 3U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY);
    gui_music_queue_window_apply_failed(&client);

    assert(gui_music_queue_window_poll(&client, true, 3U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY);
    gui_music_queue_window_mark_applied(&client, 0U, 12U, 3U);
    assert(gui_music_queue_window_poll(&client, false, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
}

/** @brief 代次 0 的空窗（拔卡、曲库重扫）：已显示的窗口要清空 Queue，一次；已清空的不再清。 */
static void test_an_empty_window_clears_a_displayed_queue_once(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    shown_window(&client, 0U, 12U, 3U);
    assert(gui_music_queue_window_poll(&client, true, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR);
    assert(gui_music_queue_window_displayed_generation(&client) == 0U);

    assert(gui_music_queue_window_poll(&client, true, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE); /* 已经清空：不再清。 */
}

/** @brief 本来就没有显示窗口时收到空窗（开机曲库没好）：不清空本来就没有的 Queue，也不记为已显示。 */
static void test_an_empty_window_before_anything_was_shown_clears_nothing(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    (void)write_if_due(&client);
    assert(gui_music_queue_window_poll(&client, true, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE);
    assert(gui_music_queue_window_displayed_generation(&client) == 0U);

    /* 曲库发布后前台主动发整窗，不用重写需求：套用即可。 */
    assert(write_if_due(&client) == -1);
    assert(gui_music_queue_window_poll(&client, true, 1U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY);
}

/** @brief 拔卡清空后重插，仍从起点 0 要第一窗：清空前写出的起点不是 0 就重写成 0；本来就是 0 不重写。 */
static void test_after_a_clear_the_request_goes_back_to_index_zero(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    shown_window(&client, 4U, 12U, 1U);
    gui_music_queue_window_note_lead(&client, 4U + GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS, 12U); /* 滚到更靠后：起点 8。 */
    assert(write_if_due(&client) == 8);

    assert(gui_music_queue_window_poll(&client, true, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR);
    assert(gui_music_queue_window_request_index(&client) == 0U);
    assert(write_if_due(&client) == 0);
    assert(write_if_due(&client) == -1);

    /* 写出的本来就是 0：清空后不用重写（前台任务按最新需求重新发窗）。 */
    gui_music_queue_window_client_init(&client);
    assert(write_if_due(&client) == 0); /* GUI 之前写出的就是 0。 */
    gui_music_queue_window_mark_applied(&client, 0U, 12U, 1U);
    assert(gui_music_queue_window_poll(&client, true, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR);
    assert(write_if_due(&client) == -1);
}

/** @brief 第一窗 Index=0；滚动未满回滑行数时不改起点。 */
static void test_scroll_within_back_rows_keeps_index_zero(void)
{
    assert(gui_music_queue_window_desired_index(0U, 8U, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS, 8U) == 0U);
}

/** @brief 滚过回滑行数再多一行，窗口起点跟上可见行，首行之上仍留回滑行。 */
static void test_scroll_past_back_rows_requests_index_one(void)
{
    assert(gui_music_queue_window_desired_index(0U, 8U, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 1U, 8U) == 1U);
}

/** @brief 窗口已含回滑行且可见首行正好在其下时保持 Index。 */
static void test_stable_when_lead_matches_back_rows(void)
{
    assert(gui_music_queue_window_desired_index(6U, 8U, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS, 8U) == 6U);
}

/** @brief 回到窗口顶部时一次退回整份回滑行（#157）：快速回滑每次换窗都补足首行之上的行，不是一窗只退一行。 */
static void test_scroll_back_to_top_requests_back_rows_earlier(void)
{
    assert(gui_music_queue_window_desired_index(6U, 8U, 0U, 8U) == (uint16_t)(6U - GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS));
}

/** @brief 列表前部不足回滑行数时退到 Index=0，不越过列表起点。 */
static void test_scroll_back_near_start_clamps_to_zero(void)
{
    assert(gui_music_queue_window_desired_index(1U, 8U, 0U, 8U) == 0U);
}

/** @brief 末窗 Length 不足上限时，不得把 Index 再往列表外推。 */
static void test_does_not_advance_index_when_window_is_short(void)
{
    assert(gui_music_queue_window_desired_index(5U, 3U, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 2U, 8U) == 5U);
}

/** @brief 列表起点没有上一窗；lead 为 0 时保持 Index=0。 */
static void test_index_zero_has_no_previous_window(void)
{
    assert(gui_music_queue_window_desired_index(0U, 8U, 0U, 8U) == 0U);
}

/** @brief 一次滚过多行只要目标 Index，不逐步 +1。 */
static void test_jump_scroll_requests_target_index_in_one_shot(void)
{
    assert(gui_music_queue_window_desired_index(0U, 8U, 7U, 8U) == (uint16_t)(7U - GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS));
}

/** @brief 已展示窗口上 lead 要求新起点时，当帧就写新起点的需求（不等上一份结果）。 */
static void test_note_lead_writes_a_new_request_when_the_desired_index_moves(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    shown_window(&client, 0U, 8U, 1U);
    gui_music_queue_window_note_lead(&client, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 1U, 8U);
    assert(gui_music_queue_window_request_index(&client) == 1U);
    assert(write_if_due(&client) == 1);
    assert(write_if_due(&client) == -1);
}

/** @brief 滚动途中起点又变：只写新的起点（新的覆盖旧的），不用等前一个窗口回来。 */
static void test_a_newer_desired_index_is_written_without_waiting_for_the_previous_window(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    shown_window(&client, 0U, 12U, 1U);
    gui_music_queue_window_note_lead(&client, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 2U, 12U);
    assert(write_if_due(&client) == 2);
    gui_music_queue_window_note_lead(&client, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 5U, 12U);
    assert(write_if_due(&client) == 5);
    gui_music_queue_window_note_lead(&client, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 5U, 12U);
    assert(write_if_due(&client) == -1); /* 起点没变：不重写。 */
}

/**
 * @brief 起点又回到已显示的窗口：把它写回需求格，撤销上一个还没答的需求。否则前台会再发一份用户已经不需要的窗口
 *        （模拟器 `queue_back_scroll` 的帧哈希就是这样发现的：多发了一窗，列表停的位置差几行）。
 */
static void test_returning_to_the_displayed_window_cancels_the_pending_request(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    shown_window(&client, 4U, 12U, 1U);
    gui_music_queue_window_note_lead(&client, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS + 5U, 12U); /* 想要起点 9。 */
    assert(write_if_due(&client) == 9);

    gui_music_queue_window_note_lead(&client, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS, 12U);      /* 滚回来：想要的又是 4。 */
    assert(gui_music_queue_window_request_index(&client) == 4U);
    assert(write_if_due(&client) == 4);
    assert(write_if_due(&client) == -1);
}

/** @brief 第一窗内滚动未越过回滑行数时，不因为滚动就重新要。 */
static void test_note_lead_does_not_rewrite_within_back_rows(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    shown_window(&client, 0U, 8U, 1U);
    gui_music_queue_window_note_lead(&client, GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS, 8U);
    assert(gui_music_queue_window_request_index(&client) == 0U);
    assert(write_if_due(&client) == -1);
}

/** @brief 套用成功后记下已显示窗口的曲库代次；换窗更新，空窗清空后回到 0（未显示）。 */
static void test_apply_records_the_displayed_window_generation(void)
{
    GUI_MusicQueueWindowClientTypeDef client;

    gui_music_queue_window_client_init(&client);
    assert(gui_music_queue_window_displayed_generation(&client) == 0U);

    gui_music_queue_window_mark_applied(&client, 0U, 8U, 7U);
    assert(gui_music_queue_window_displayed_generation(&client) == 7U);

    gui_music_queue_window_mark_applied(&client, 4U, 8U, 9U);
    assert(gui_music_queue_window_displayed_generation(&client) == 9U);

    assert(gui_music_queue_window_poll(&client, true, 0U) == GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR);
    assert(gui_music_queue_window_displayed_generation(&client) == 0U);
}

/**
 * @brief 运行全部窗口客户测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_writes_the_first_window_request_right_away();
    test_a_failed_write_is_retried_on_the_next_frame();
    test_applies_a_new_window_and_does_not_ask_again();
    test_a_window_for_an_older_request_is_applied_like_any_other();
    test_a_failed_apply_is_retried_from_the_gui_copy_without_asking_again();
    test_a_newer_window_replaces_a_failed_apply();
    test_an_empty_window_clears_a_displayed_queue_once();
    test_an_empty_window_before_anything_was_shown_clears_nothing();
    test_after_a_clear_the_request_goes_back_to_index_zero();
    test_scroll_within_back_rows_keeps_index_zero();
    test_scroll_past_back_rows_requests_index_one();
    test_stable_when_lead_matches_back_rows();
    test_scroll_back_to_top_requests_back_rows_earlier();
    test_scroll_back_near_start_clamps_to_zero();
    test_does_not_advance_index_when_window_is_short();
    test_index_zero_has_no_previous_window();
    test_jump_scroll_requests_target_index_in_one_shot();
    test_note_lead_writes_a_new_request_when_the_desired_index_moves();
    test_a_newer_desired_index_is_written_without_waiting_for_the_previous_window();
    test_returning_to_the_displayed_window_cancels_the_pending_request();
    test_note_lead_does_not_rewrite_within_back_rows();
    test_apply_records_the_displayed_window_generation();
    puts("gui_music_queue_window_tests: all tests passed");
    return 0;
}
