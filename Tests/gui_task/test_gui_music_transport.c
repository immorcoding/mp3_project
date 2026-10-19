/**
 ******************************************************************************
 * @file    test_gui_music_transport.c
 * @brief   音乐页快照归约（C1–C8、B5、B6）的主机行为测试。
 *
 * @details
 *          输入是快照序列、最后发出的命令序号与已显示窗口代次，输出是显示决定；
 *          命令发送用本文件里假的 playback_command_send() 替身。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/gui/music/gui_music_transport.h"

static Playback_StatusTypeDef fake_send_status;
static Playback_CommandTypeDef fake_last_command;
static unsigned fake_send_calls;

/**
 * @brief 假的 playback_command_send()：记下最后一条命令并返回预设结果。
 * @param[in] command 待发送的命令。
 * @return 测试预设的 fake_send_status。
 */
Playback_StatusTypeDef playback_command_send(const Playback_CommandTypeDef *command)
{
    fake_send_calls++;
    fake_last_command = *command;
    return fake_send_status;
}

/**
 * @brief 假的 playback_snapshot_read()：本测试不经它取快照，仅满足合同头的声明。
 * @param[out] snapshot 被清零的快照。
 */
void playback_snapshot_read(Playback_SnapshotTypeDef *snapshot)
{
    (void)memset(snapshot, 0, sizeof(*snapshot));
}

/**
 * @brief 复位假发送替身。
 */
static void fake_send_reset(void)
{
    fake_send_status = PLAYBACK_OK;
    (void)memset(&fake_last_command, 0, sizeof(fake_last_command));
    fake_send_calls = 0U;
}

/**
 * @brief 构造一份已追上回显的快照（HandledCommandSeq 为 0，与初始状态一致）。
 * @param[in] state 三态。
 * @param[in] has_track 是否有当前曲。
 * @param[in] generation 当前曲曲库代次。
 * @param[in] index 当前曲位置。
 * @return 快照；其余字段为 0。
 */
static Playback_SnapshotTypeDef make_snapshot(
    Playback_StateTypeDef state,
    bool has_track,
    uint32_t generation,
    uint16_t index)
{
    Playback_SnapshotTypeDef snapshot;

    (void)memset(&snapshot, 0, sizeof(snapshot));
    snapshot.State = state;
    snapshot.HasTrack = has_track;
    snapshot.Generation = has_track ? generation : 0U;
    snapshot.Index = has_track ? index : 0U;
    return snapshot;
}

/**
 * @brief 归约一份快照并返回决定。
 * @param[in,out] transport 归约状态。
 * @param[in] snapshot 快照。
 * @param[in] window_generation 已显示窗口代次。
 * @return 显示决定。
 */
static GUI_MusicTransportDecisionTypeDef reduce(
    GUI_MusicTransportTypeDef *transport,
    const Playback_SnapshotTypeDef *snapshot,
    uint32_t window_generation)
{
    GUI_MusicTransportDecisionTypeDef decision;

    gui_music_transport_reduce(transport, snapshot, window_generation, &decision);
    return decision;
}

/**
 * @brief 进度按已解码时间除以总时长取百分比，时间原样输出。
 */
static void test_progress_and_time_follow_snapshot(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 1U, 2U);

    gui_music_transport_init(&transport);
    snapshot.PositionMs = 65000U;
    snapshot.DurationMs = 194000U;
    decision = reduce(&transport, &snapshot, 1U);

    assert(decision.state == PLAYBACK_STATE_PLAYING);
    assert(decision.progress_percent == 33U);
    assert(decision.position_ms == 65000U);
    assert(decision.duration_ms == 194000U);
}

/**
 * @brief 总时长未知时进度停在 0，时间仍显示已解码时间。
 */
static void test_unknown_duration_keeps_progress_at_zero(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 1U, 0U);

    gui_music_transport_init(&transport);
    snapshot.PositionMs = 42000U;
    snapshot.DurationMs = 0U;
    decision = reduce(&transport, &snapshot, 1U);

    assert(decision.progress_percent == 0U);
    assert(decision.position_ms == 42000U);
    assert(decision.duration_ms == 0U);
}

/**
 * @brief 位置超过总时长时进度钳在 100。
 */
static void test_progress_clamps_position_beyond_duration(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 1U, 0U);

    gui_music_transport_init(&transport);
    snapshot.PositionMs = 200000U;
    snapshot.DurationMs = 100000U;
    decision = reduce(&transport, &snapshot, 1U);

    assert(decision.progress_percent == 100U);
}

/**
 * @brief 位置与总时长很大时按 64 位计算，不因乘 100 溢出。
 */
static void test_progress_does_not_overflow_with_large_times(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 1U, 0U);

    gui_music_transport_init(&transport);
    snapshot.PositionMs = 0x80000000U;
    snapshot.DurationMs = 0xFFFFFFFFU;
    decision = reduce(&transport, &snapshot, 1U);

    assert(decision.progress_percent == 50U);
}

/**
 * @brief 发送成功的命令按 1、2、3 递增编号，并带上种类与 SELECT 参数。
 */
static void test_send_numbers_successful_commands_in_order(void)
{
    GUI_MusicTransportTypeDef transport;

    fake_send_reset();
    gui_music_transport_init(&transport);

    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_OK);
    assert(fake_last_command.Kind == PLAYBACK_COMMAND_NEXT);
    assert(fake_last_command.Seq == 1U);

    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_SELECT, 7U, 3U) == PLAYBACK_OK);
    assert(fake_last_command.Kind == PLAYBACK_COMMAND_SELECT);
    assert(fake_last_command.Seq == 2U);
    assert(fake_last_command.Generation == 7U);
    assert(fake_last_command.Index == 3U);
}

/**
 * @brief 队列满的命令不占序号：下一条成功的命令接着上一条成功的序号，回显不会永远追不上。
 */
static void test_send_failure_does_not_consume_a_sequence_number(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PAUSED, true, 1U, 0U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_TOGGLE, 0U, 0U) == PLAYBACK_OK);

    fake_send_status = PLAYBACK_QUEUE_FULL;
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_TOGGLE, 0U, 0U) == PLAYBACK_QUEUE_FULL);

    fake_send_status = PLAYBACK_OK;
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_TOGGLE, 0U, 0U) == PLAYBACK_OK);
    assert(fake_last_command.Seq == 2U);

    snapshot.HandledCommandSeq = 2U;
    decision = reduce(&transport, &snapshot, 1U);
    assert(decision.caught_up == true);
}

/**
 * @brief 只有发送失败过的命令不让界面冻结：没有成功发出的命令就没有要等的回显。
 */
static void test_failed_send_alone_does_not_freeze(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PAUSED, true, 1U, 0U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 1U);

    fake_send_status = PLAYBACK_QUEUE_FULL;
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_QUEUE_FULL);

    snapshot.State = PLAYBACK_STATE_PLAYING;
    decision = reduce(&transport, &snapshot, 1U);
    assert(decision.caught_up == true);
    assert(decision.state == PLAYBACK_STATE_PLAYING);
}

/**
 * @brief 回显未追上时，三态、曲目身份与总时长冻结，只有位置照用当前快照。
 */
static void test_unreached_echo_freezes_state_identity_and_duration(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PAUSED, true, 5U, 3U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    snapshot.PositionMs = 30000U;
    snapshot.DurationMs = 100000U;
    (void)reduce(&transport, &snapshot, 5U);

    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_OK);

    /* 后端尚未处理该命令（Handled 仍是 0），快照却已因别的原因变了样。 */
    snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 4U);
    snapshot.PositionMs = 1000U;
    snapshot.DurationMs = 0U;
    decision = reduce(&transport, &snapshot, 5U);

    assert(decision.caught_up == false);
    assert(decision.state == PLAYBACK_STATE_PAUSED);
    assert(decision.duration_ms == 100000U);
    assert(decision.position_ms == 1000U);
    assert(decision.progress_percent == 1U);
}

/**
 * @brief 回显追上后立即采信新快照的全部字段。
 */
static void test_echo_catching_up_adopts_the_new_snapshot(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PAUSED, true, 5U, 3U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 5U);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_OK);

    snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 4U);
    snapshot.HandledCommandSeq = 1U;
    snapshot.DurationMs = 80000U;
    decision = reduce(&transport, &snapshot, 5U);

    assert(decision.caught_up == true);
    assert(decision.state == PLAYBACK_STATE_PLAYING);
    assert(decision.duration_ms == 80000U);
}

/**
 * @brief 序号按有符号差比较：回绕后回显 0 追上最后发出的 0，旧回显 0xFFFFFFFE 仍未追上。
 */
static void test_sequence_comparison_survives_wraparound(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PAUSED, true, 1U, 0U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    transport.last_sent_seq = 0xFFFFFFFEU; /* 头里公开的状态字段：把计数推到回绕前。 */
    (void)reduce(&transport, &snapshot, 1U);

    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_TOGGLE, 0U, 0U) == PLAYBACK_OK);
    assert(fake_last_command.Seq == 0xFFFFFFFFU);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_TOGGLE, 0U, 0U) == PLAYBACK_OK);
    assert(fake_last_command.Seq == 0U);

    snapshot.HandledCommandSeq = 0xFFFFFFFEU;
    decision = reduce(&transport, &snapshot, 1U);
    assert(decision.caught_up == false);

    snapshot.HandledCommandSeq = 0U;
    decision = reduce(&transport, &snapshot, 1U);
    assert(decision.caught_up == true);
}

/**
 * @brief 回显已追上、有当前曲、代次非 0 且等于窗口代次时，高亮当前曲的位置。
 */
static void test_highlight_follows_current_track_in_displayed_window(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 3U);

    gui_music_transport_init(&transport);
    decision = reduce(&transport, &snapshot, 5U);

    assert(decision.highlight_apply == true);
    assert(decision.highlight_index == 3U);
}

/**
 * @brief 快照代次与窗口代次不同（曲库已变）时不高亮任何行，但要覆盖旧高亮。
 */
static void test_highlight_is_empty_when_generations_differ(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 3U);

    gui_music_transport_init(&transport);
    decision = reduce(&transport, &snapshot, 4U);

    assert(decision.highlight_apply == true);
    assert(decision.highlight_index == GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 开启尚未确认（代次 0）时，即使窗口代次也是 0（未显示窗口）也不高亮。
 */
static void test_highlight_ignores_unconfirmed_generation_zero(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 0U, 3U);

    gui_music_transport_init(&transport);
    decision = reduce(&transport, &snapshot, 0U);

    assert(decision.highlight_apply == true);
    assert(decision.highlight_index == GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 无当前曲（如拔卡）时不高亮。
 */
static void test_highlight_is_empty_without_current_track(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_STOPPED, false, 0U, 0U);

    gui_music_transport_init(&transport);
    decision = reduce(&transport, &snapshot, 5U);

    assert(decision.highlight_apply == true);
    assert(decision.highlight_index == GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 回显未追上时不覆盖 Service 的本行即时高亮。
 */
static void test_highlight_does_not_override_while_echo_is_behind(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 3U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 5U);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_SELECT, 5U, 6U) == PLAYBACK_OK);

    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.caught_up == false);
    assert(decision.highlight_apply == false);
}

/**
 * @brief 队列满撤回点行高亮：按最后一份已追上的快照套高亮条件，不受未追上的限制。
 */
static void test_withdraw_highlight_uses_last_caught_up_snapshot(void)
{
    GUI_MusicTransportTypeDef transport;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 3U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 5U);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_OK);

    /* 回显落后，快照里已是别的曲目；撤回仍以冻结的那份为准。 */
    snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 4U);
    (void)reduce(&transport, &snapshot, 5U);

    assert(gui_music_transport_withdraw_highlight(&transport, 5U) == 3U);
    assert(gui_music_transport_withdraw_highlight(&transport, 6U) ==
           GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 还没有任何已追上的快照时，撤回得到无高亮。
 */
static void test_withdraw_highlight_is_empty_before_any_snapshot(void)
{
    GUI_MusicTransportTypeDef transport;

    gui_music_transport_init(&transport);
    assert(gui_music_transport_withdraw_highlight(&transport, 5U) ==
           GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 构造一份带停止原因的停止态快照。
 * @param[in] reason 停止原因。
 * @param[in] stop_count 开机以来进入停止的次数。
 * @param[in] has_track 是否保留当前曲。
 * @param[in] index 当前曲位置。
 * @return 快照。
 */
static Playback_SnapshotTypeDef make_stopped(
    Playback_StopReasonTypeDef reason,
    uint32_t stop_count,
    bool has_track,
    uint16_t index)
{
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_STOPPED, has_track, 5U, index);

    snapshot.StopReason = reason;
    snapshot.StopCount = stop_count;
    return snapshot;
}

/**
 * @brief 九个非 NONE 停止原因各对应一条英文短语，NONE 回到时间（短语为空）。
 */
static void test_each_stop_reason_shows_its_phrase(void)
{
    static const struct
    {
        Playback_StopReasonTypeDef reason;
        const char *phrase;
    } table[] =
    {
        { PLAYBACK_STOP_REASON_CARD_REMOVED, "No SD card" },
        { PLAYBACK_STOP_REASON_NOT_READY, "SD card not ready" },
        { PLAYBACK_STOP_REASON_NO_TRACK, "No tracks" },
        { PLAYBACK_STOP_REASON_CATALOG_CHANGED, "Library changed" },
        { PLAYBACK_STOP_REASON_READ_ERROR, "Read error" },
        { PLAYBACK_STOP_REASON_NO_DATA, "Read timeout" },
        { PLAYBACK_STOP_REASON_AUDIO_ERROR, "Audio error" },
        { PLAYBACK_STOP_REASON_AUDIO_FAULT, "Audio fault, restart" },
        { PLAYBACK_STOP_REASON_ALL_BAD, "No playable files" }
    };
    size_t i;

    for (i = 0U; i < (sizeof(table) / sizeof(table[0])); i++)
    {
        GUI_MusicTransportTypeDef transport;
        GUI_MusicTransportDecisionTypeDef decision;
        Playback_SnapshotTypeDef snapshot = make_stopped(table[i].reason, 1U, true, 2U);

        gui_music_transport_init(&transport);
        decision = reduce(&transport, &snapshot, 5U);
        assert(decision.status_phrase != NULL);
        assert(strcmp(decision.status_phrase, table[i].phrase) == 0);
    }
}

/**
 * @brief 开机后从未播过（原因 NONE）不显示短语，状态行留给时间。
 */
static void test_stopped_without_reason_shows_no_phrase(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_stopped(PLAYBACK_STOP_REASON_NONE, 0U, false, 0U);

    gui_music_transport_init(&transport);
    decision = reduce(&transport, &snapshot, 0U);
    assert(decision.status_phrase == NULL);
}

/**
 * @brief 停止原因常驻到离开停止态；再次带原因进入停止（StopCount 增加）又显示。
 */
static void test_phrase_stays_until_leaving_stopped_state(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_stopped(PLAYBACK_STOP_REASON_READ_ERROR, 1U, true, 2U);

    gui_music_transport_init(&transport);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase != NULL);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase != NULL);

    snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 2U);
    snapshot.StopCount = 1U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase == NULL);

    snapshot = make_stopped(PLAYBACK_STOP_REASON_NO_DATA, 2U, true, 2U);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase != NULL);
    assert(strcmp(decision.status_phrase, "Read timeout") == 0);
}

/**
 * @brief 停止期间曲目身份变化（停止后按上一首/下一首）清除短语，并且不会在下一圈复现。
 */
static void test_identity_change_during_stop_clears_phrase(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_stopped(PLAYBACK_STOP_REASON_READ_ERROR, 1U, true, 2U);

    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 5U);

    snapshot = make_stopped(PLAYBACK_STOP_REASON_READ_ERROR, 1U, true, 3U);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase == NULL);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase == NULL);
}

/**
 * @brief 进入停止的同一份快照里身份变为无当前曲（拔卡）不算身份变化，短语一直显示。
 */
static void test_card_removed_losing_the_track_does_not_clear_phrase(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 2U);

    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 5U);

    snapshot = make_stopped(PLAYBACK_STOP_REASON_CARD_REMOVED, 1U, false, 0U);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase != NULL);
    assert(strcmp(decision.status_phrase, "No SD card") == 0);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase != NULL);
}

/**
 * @brief 清除后原因不变地再次进入停止（音频故障锁存后再按播放）短语重新出现。
 */
static void test_repeated_stop_shows_phrase_again_after_clear(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_stopped(PLAYBACK_STOP_REASON_AUDIO_FAULT, 1U, true, 2U);

    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 5U);
    snapshot = make_stopped(PLAYBACK_STOP_REASON_AUDIO_FAULT, 1U, true, 3U);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase == NULL);

    snapshot = make_stopped(PLAYBACK_STOP_REASON_AUDIO_FAULT, 2U, true, 3U);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_phrase != NULL);
    assert(strcmp(decision.status_phrase, "Audio fault, restart") == 0);
}

/**
 * @brief 回显未追上时停止原因按冻结的快照显示，不被中间快照改写。
 */
static void test_phrase_follows_frozen_snapshot(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_stopped(PLAYBACK_STOP_REASON_READ_ERROR, 1U, true, 2U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 5U);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_OK);

    snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 3U);
    snapshot.StopCount = 1U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.caught_up == false);
    assert(decision.status_phrase != NULL);
    assert(strcmp(decision.status_phrase, "Read error") == 0);
}

/**
 * @brief 首次读到快照只记基线：开机或重进页面时不补发旧的跳过与停止提示。
 */
static void test_first_snapshot_sets_count_baseline_without_notice(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    gui_music_transport_init(&transport);
    snapshot.SkippedBadCount = 5U;
    snapshot.StopCount = 3U;
    decision = reduce(&transport, &snapshot, 5U);

    assert(decision.skip_added == 0U);
    assert(decision.status_flash == false);
}

/**
 * @brief 之后跳过计数的差值只报告一次。
 */
static void test_skipped_count_difference_is_reported_once(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    gui_music_transport_init(&transport);
    snapshot.SkippedBadCount = 5U;
    (void)reduce(&transport, &snapshot, 5U);

    snapshot.SkippedBadCount = 7U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.skip_added == 2U);

    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.skip_added == 0U);
}

/**
 * @brief 同一圈停止次数增加多次也只闪一次，下一圈不再闪。
 */
static void test_several_stops_in_one_lap_flash_once(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    gui_music_transport_init(&transport);
    snapshot.StopCount = 3U;
    (void)reduce(&transport, &snapshot, 5U);

    snapshot.StopCount = 6U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_flash == true);

    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.status_flash == false);
}

/**
 * @brief 计数在回显未追上时照用：冻结期间报告的差值追上后不再重复。
 */
static void test_counts_are_used_while_frozen_and_not_repeated(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 5U);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_OK);

    snapshot.SkippedBadCount = 1U;
    snapshot.StopCount = 1U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.caught_up == false);
    assert(decision.skip_added == 1U);
    assert(decision.status_flash == true);

    snapshot.HandledCommandSeq = 1U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.caught_up == true);
    assert(decision.skip_added == 0U);
    assert(decision.status_flash == false);
}

/**
 * @brief 唱盘只在显示的三态为播放时转，暂停与停止都停在原角度。
 */
static void test_vinyl_spins_only_while_playing(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    gui_music_transport_init(&transport);
    snapshot.PositionMs = 1000U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_playing == true);

    snapshot.State = PLAYBACK_STATE_PAUSED;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_playing == false);
    assert(decision.vinyl_reset_angle == false);

    snapshot.State = PLAYBACK_STATE_PLAYING;
    snapshot.PositionMs = 1500U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_playing == true);
    assert(decision.vinyl_reset_angle == false);
}

/**
 * @brief 首次读到快照不归零角度（初始化时已归零），位置前进也不归零。
 */
static void test_vinyl_keeps_angle_on_first_snapshot_and_forward_progress(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    gui_music_transport_init(&transport);
    snapshot.PositionMs = 90000U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_reset_angle == false);

    snapshot.PositionMs = 91000U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_reset_angle == false);
}

/**
 * @brief 曲目身份变化（切歌）时角度归零，同一首的后续快照不再归零。
 */
static void test_vinyl_resets_when_track_identity_changes(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 5U);

    snapshot.Index = 2U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_reset_angle == true);
    assert(decision.vinyl_playing == true);

    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_reset_angle == false);
}

/**
 * @brief 位置变小（同曲重选、停止归零）时角度归零。
 */
static void test_vinyl_resets_when_position_goes_backwards(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    gui_music_transport_init(&transport);
    snapshot.PositionMs = 60000U;
    (void)reduce(&transport, &snapshot, 5U);

    snapshot.PositionMs = 200U; /* 同曲重选：同一身份，位置回到开头。 */
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_reset_angle == true);

    snapshot = make_stopped(PLAYBACK_STOP_REASON_READ_ERROR, 1U, true, 1U);
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_reset_angle == true);
    assert(decision.vinyl_playing == false);
}

/**
 * @brief 回显未追上时曲目身份冻结，唱盘不因快照里提前换了曲而归零。
 */
static void test_vinyl_does_not_reset_on_identity_while_frozen(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    snapshot.PositionMs = 1000U;
    (void)reduce(&transport, &snapshot, 5U);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_OK);

    snapshot.Index = 2U;
    snapshot.PositionMs = 1200U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_reset_angle == false);

    snapshot.HandledCommandSeq = 1U;
    decision = reduce(&transport, &snapshot, 5U);
    assert(decision.vinyl_reset_angle == true);
}

/**
 * @brief 接线回归：开机从停止态按播放，后端先报“开启中、代次 0”，应答后确认代次与总时长。
 * @note  场景同模拟器 music_snapshot：开启中无高亮、时间 0/未知；确认代次时按 C8 把
 *        0 -> 非 0 当作身份变化归零一次唱盘（此时位置仍为 0）。
 */
static void test_play_from_boot_confirms_generation_after_opening(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_STOPPED, false, 0U, 0U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    decision = reduce(&transport, &snapshot, 1U);
    assert(decision.state == PLAYBACK_STATE_STOPPED);
    assert(decision.highlight_index == GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
    assert(decision.status_phrase == NULL);

    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_TOGGLE, 0U, 0U) == PLAYBACK_OK);
    snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 0U, 0U);
    snapshot.HandledCommandSeq = 1U;
    decision = reduce(&transport, &snapshot, 1U);
    assert(decision.caught_up == true);
    assert(decision.state == PLAYBACK_STATE_PLAYING);
    assert(decision.highlight_apply == true);
    assert(decision.highlight_index == GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
    assert(decision.duration_ms == 0U);
    assert(decision.progress_percent == 0U);
    assert(decision.vinyl_playing == true);

    snapshot.Generation = 1U;
    snapshot.DurationMs = 150000U;
    decision = reduce(&transport, &snapshot, 1U);
    assert(decision.highlight_index == 0U);
    assert(decision.duration_ms == 150000U);
    assert(decision.vinyl_reset_angle == true);

    snapshot.PositionMs = 1500U;
    decision = reduce(&transport, &snapshot, 1U);
    assert(decision.vinyl_reset_angle == false);
    assert(decision.progress_percent == 1U);
}

/**
 * @brief 接线回归：回显落后时换上新窗口，新窗口的高亮仍按最后一份已追上的快照判定。
 * @note  gui_music 在 APPLY 时用 withdraw_highlight(新窗口代次) 套高亮，与 B6 撤回同一规则。
 */
static void test_new_window_highlight_uses_last_caught_up_snapshot(void)
{
    GUI_MusicTransportTypeDef transport;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 1U, 3U);

    fake_send_reset();
    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 1U);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_OK);

    snapshot.Index = 4U;
    (void)reduce(&transport, &snapshot, 1U);

    assert(gui_music_transport_withdraw_highlight(&transport, 1U) == 3U);
    assert(gui_music_transport_withdraw_highlight(&transport, 2U) == GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 接线回归：挂起的后端被点行填满 FIFO，第 9 条被拒后撤回到冻结的曲目；恢复后一次处理
 *        完全部命令，回显追上，高亮改为最后一条 SELECT 的位置（场景 music_echo）。
 */
static void test_queue_full_withdraw_then_catch_up_highlights_last_select(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 1U, 0U);
    uint16_t row;

    fake_send_reset();
    gui_music_transport_init(&transport);
    (void)reduce(&transport, &snapshot, 1U);

    for (row = 1U; row <= 8U; row++)
    {
        assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_SELECT, 1U, row) == PLAYBACK_OK);
        decision = reduce(&transport, &snapshot, 1U);
        assert(decision.highlight_apply == false);
    }

    fake_send_status = PLAYBACK_QUEUE_FULL;
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_SELECT, 1U, 9U) == PLAYBACK_QUEUE_FULL);
    assert(gui_music_transport_withdraw_highlight(&transport, 1U) == 0U);

    snapshot.Index = 8U;
    snapshot.HandledCommandSeq = 8U;
    decision = reduce(&transport, &snapshot, 1U);
    assert(decision.caught_up == true);
    assert(decision.highlight_apply == true);
    assert(decision.highlight_index == 8U);
}

/**
 * @brief 空指针不崩溃：归约输出全零且无高亮，发送按失败处理且不调用后端。
 */
static void test_null_arguments_are_safe(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_snapshot(PLAYBACK_STATE_PLAYING, true, 5U, 1U);

    fake_send_reset();
    gui_music_transport_init(NULL);
    gui_music_transport_init(&transport);
    gui_music_transport_reduce(NULL, &snapshot, 5U, &decision);
    assert(decision.highlight_apply == false);
    assert(decision.highlight_index == GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
    gui_music_transport_reduce(&transport, NULL, 5U, &decision);
    assert(decision.status_phrase == NULL);
    gui_music_transport_reduce(&transport, &snapshot, 5U, NULL);
    assert(gui_music_transport_send(NULL, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_QUEUE_FULL);
    assert(fake_send_calls == 0U);
    assert(gui_music_transport_withdraw_highlight(NULL, 5U) == GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT);
}

/**
 * @brief 运行全部快照归约测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    fake_send_reset();
    test_progress_and_time_follow_snapshot();
    test_unknown_duration_keeps_progress_at_zero();
    test_progress_clamps_position_beyond_duration();
    test_progress_does_not_overflow_with_large_times();
    test_send_numbers_successful_commands_in_order();
    test_send_failure_does_not_consume_a_sequence_number();
    test_failed_send_alone_does_not_freeze();
    test_unreached_echo_freezes_state_identity_and_duration();
    test_echo_catching_up_adopts_the_new_snapshot();
    test_sequence_comparison_survives_wraparound();
    test_highlight_follows_current_track_in_displayed_window();
    test_highlight_is_empty_when_generations_differ();
    test_highlight_ignores_unconfirmed_generation_zero();
    test_highlight_is_empty_without_current_track();
    test_highlight_does_not_override_while_echo_is_behind();
    test_withdraw_highlight_uses_last_caught_up_snapshot();
    test_withdraw_highlight_is_empty_before_any_snapshot();
    test_each_stop_reason_shows_its_phrase();
    test_stopped_without_reason_shows_no_phrase();
    test_phrase_stays_until_leaving_stopped_state();
    test_identity_change_during_stop_clears_phrase();
    test_card_removed_losing_the_track_does_not_clear_phrase();
    test_repeated_stop_shows_phrase_again_after_clear();
    test_phrase_follows_frozen_snapshot();
    test_first_snapshot_sets_count_baseline_without_notice();
    test_skipped_count_difference_is_reported_once();
    test_several_stops_in_one_lap_flash_once();
    test_counts_are_used_while_frozen_and_not_repeated();
    test_vinyl_spins_only_while_playing();
    test_vinyl_keeps_angle_on_first_snapshot_and_forward_progress();
    test_vinyl_resets_when_track_identity_changes();
    test_vinyl_resets_when_position_goes_backwards();
    test_vinyl_does_not_reset_on_identity_while_frozen();
    test_play_from_boot_confirms_generation_after_opening();
    test_new_window_highlight_uses_last_caught_up_snapshot();
    test_queue_full_withdraw_then_catch_up_highlights_last_select();
    test_null_arguments_are_safe();

    puts("gui_music_transport_tests: all tests passed");
    return 0;
}
