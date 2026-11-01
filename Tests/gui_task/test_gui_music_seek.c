/**
 ******************************************************************************
 * @file    test_gui_music_seek.c
 * @brief   Now Playing 跳转归约（GUI-10、GUI-17、GUI-20，#172 B 节）的主机行为测试。
 *
 * @details
 *          输入是进度条手势（按下 / 拖动 / 松手 / 取消，千分比）与快照序列，输出是
 *          显示决定与发出的 SEEK 命令；命令发送用本文件里假的 playback_command_send()。
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
 * @brief 构造一份正在播放、总时长已知的快照（回显序号 0）。
 * @param[in] index 当前曲位置（代次固定为 3）。
 * @param[in] position_ms 已解码时间。
 * @param[in] duration_ms 总时长；0 = 未知。
 * @return 快照。
 */
static Playback_SnapshotTypeDef make_playing(uint16_t index, uint32_t position_ms, uint32_t duration_ms)
{
    Playback_SnapshotTypeDef snapshot;

    (void)memset(&snapshot, 0, sizeof(snapshot));
    snapshot.State = PLAYBACK_STATE_PLAYING;
    snapshot.HasTrack = true;
    snapshot.Generation = 3U;
    snapshot.Index = index;
    snapshot.PositionMs = position_ms;
    snapshot.DurationMs = duration_ms;
    snapshot.TrackStartCount = 1U;
    return snapshot;
}

/**
 * @brief 归约一份快照并返回决定。
 * @param[in,out] transport 归约状态。
 * @param[in] snapshot 快照。
 * @return 显示决定（窗口代次固定为 3）。
 */
static GUI_MusicTransportDecisionTypeDef reduce(
    GUI_MusicTransportTypeDef *transport,
    const Playback_SnapshotTypeDef *snapshot)
{
    GUI_MusicTransportDecisionTypeDef decision;

    gui_music_transport_reduce(transport, snapshot, 3U, &decision);
    return decision;
}

/**
 * @brief 准备一个已显示过一份快照的归约状态。
 * @param[out] transport 归约状态。
 * @param[in] snapshot 首份快照。
 * @return 首份快照的决定。
 */
static GUI_MusicTransportDecisionTypeDef start(
    GUI_MusicTransportTypeDef *transport,
    const Playback_SnapshotTypeDef *snapshot)
{
    fake_send_reset();
    gui_music_transport_init(transport);
    return reduce(transport, snapshot);
}

/**
 * @brief 拖动中时间预览“目标/按下时的总时长”，进度跟手。
 */
static void test_drag_previews_target_over_bound_duration(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 100U);
    gui_music_transport_seek_drag(&transport, 700U);

    snapshot.PositionMs = 40500U;
    decision = reduce(&transport, &snapshot);
    assert(decision.position_ms == 140000U);
    assert(decision.duration_ms == 200000U);
    assert(decision.progress_percent == 70U);
    assert(fake_send_calls == 0U);
}

/**
 * @brief 松手发 SEEK：身份是按下时显示的曲目，目标 = 松手比例 × 按下时的总时长。
 */
static void test_release_sends_seek_bound_to_pressed_track(void)
{
    GUI_MusicTransportTypeDef transport;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 100U);
    gui_music_transport_seek_drag(&transport, 600U);
    assert(gui_music_transport_seek_release(&transport, 650U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);

    assert(fake_send_calls == 1U);
    assert(fake_last_command.Kind == PLAYBACK_COMMAND_SEEK);
    assert(fake_last_command.Seq == 1U);
    assert(fake_last_command.Generation == 3U);
    assert(fake_last_command.Index == 2U);
    assert(fake_last_command.TargetMs == 130000U);
}

/**
 * @brief 比例两端：0 发 TargetMs 0，满量程发整段总时长，超出满量程夹到满量程。
 */
static void test_release_at_both_ends(void)
{
    GUI_MusicTransportTypeDef transport;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 0U);
    assert(gui_music_transport_seek_release(&transport, 0U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);
    assert(fake_last_command.TargetMs == 0U);

    gui_music_transport_seek_press(&transport, 0U);
    assert(gui_music_transport_seek_release(&transport, 5000U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);
    assert(fake_last_command.TargetMs == 200000U);
}

/**
 * @brief 发送成功后位置与进度停在目标，直到回显序号追上，之后按快照（落点）显示。
 */
static void test_position_holds_target_until_echo_catches_up(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 500U);
    assert(gui_music_transport_seek_release(&transport, 500U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);

    snapshot.PositionMs = 41000U;
    decision = reduce(&transport, &snapshot);
    assert(decision.seek_dragging == false);
    assert(decision.position_ms == 100000U);
    assert(decision.duration_ms == 200000U);
    assert(decision.progress_percent == 50U);

    snapshot.PositionMs = 98000U; /* 落点略早于目标。 */
    snapshot.HandledCommandSeq = 1U;
    decision = reduce(&transport, &snapshot);
    assert(decision.position_ms == 98000U);
    assert(decision.progress_percent == 49U);

    snapshot.PositionMs = 99000U;
    decision = reduce(&transport, &snapshot);
    assert(decision.position_ms == 99000U);
}

/**
 * @brief 连续松手显示最后一次的目标；回显追上较早那条时仍保持最后一条。
 */
static void test_consecutive_releases_hold_the_last_target(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 300U);
    assert(gui_music_transport_seek_release(&transport, 300U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);
    (void)reduce(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 800U);
    assert(gui_music_transport_seek_release(&transport, 800U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);
    assert(fake_last_command.Seq == 2U);

    snapshot.HandledCommandSeq = 1U;
    snapshot.PositionMs = 60000U;
    decision = reduce(&transport, &snapshot);
    assert(decision.position_ms == 160000U);

    snapshot.HandledCommandSeq = 2U;
    snapshot.PositionMs = 160000U;
    decision = reduce(&transport, &snapshot);
    assert(decision.position_ms == 160000U);
}

/**
 * @brief 命令队列满：丢弃本次输入，位置回到快照，不保持目标，回显也不被冻结。
 */
static void test_queue_full_drops_release_and_shows_snapshot(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    fake_send_status = PLAYBACK_QUEUE_FULL;
    gui_music_transport_seek_press(&transport, 500U);
    assert(gui_music_transport_seek_release(&transport, 500U) == GUI_MUSIC_TRANSPORT_SEEK_QUEUE_FULL);

    snapshot.PositionMs = 40500U;
    decision = reduce(&transport, &snapshot);
    assert(decision.caught_up == true);
    assert(decision.seek_dragging == false);
    assert(decision.position_ms == 40500U);
    assert(decision.progress_percent == 20U);
}

/**
 * @brief 在途跳转期间显示的曲目已不是目标所属曲目（之后按 NEXT 并追上）时不再保持目标。
 */
static void test_hold_applies_only_while_displayed_track_is_the_target_track(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 500U);
    assert(gui_music_transport_seek_release(&transport, 500U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);
    assert(gui_music_transport_send(&transport, PLAYBACK_COMMAND_NEXT, 0U, 0U) == PLAYBACK_OK);

    decision = reduce(&transport, &snapshot);
    assert(decision.position_ms == 100000U);

    /* 后端先处理 SEEK（回显 1）再处理 NEXT：回显仍落后于 NEXT，身份冻结在第 2 首。 */
    snapshot = make_playing(3U, 0U, 0U);
    snapshot.TrackStartCount = 2U;
    snapshot.HandledCommandSeq = 1U;
    decision = reduce(&transport, &snapshot);
    assert(decision.caught_up == false);
    assert(decision.position_ms == 0U); /* 回显已追上 SEEK，不再保持目标；位置照用当前快照。 */

    snapshot.HandledCommandSeq = 2U;
    decision = reduce(&transport, &snapshot);
    assert(decision.index == 3U);
    assert(decision.position_ms == 0U);
}

/**
 * @brief 拖动中显示的曲目身份变化（播完前进）即取消拖动：本圈给出一次取消事件，时间与进度
 *        按新曲快照显示，之后松手不发命令。
 */
static void test_track_change_while_dragging_cancels_and_release_sends_nothing(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 198000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 300U);
    gui_music_transport_seek_drag(&transport, 400U);
    decision = reduce(&transport, &snapshot);
    assert(decision.seek_dragging == true);
    assert(decision.seek_cancel == false);

    snapshot = make_playing(3U, 0U, 210000U);
    snapshot.TrackStartCount = 2U;
    decision = reduce(&transport, &snapshot);
    assert(decision.seek_cancel == true);
    assert(decision.seek_dragging == false);
    assert(decision.position_ms == 0U);
    assert(decision.duration_ms == 210000U);
    assert(decision.progress_percent == 0U);

    decision = reduce(&transport, &snapshot);
    assert(decision.seek_cancel == false);

    gui_music_transport_seek_drag(&transport, 900U);
    assert(gui_music_transport_seek_release(&transport, 900U) == GUI_MUSIC_TRANSPORT_SEEK_NOT_SENT);
    assert(fake_send_calls == 0U);
}

/**
 * @brief 拖动中进入停止（总时长变为未知）同样取消拖动，之后松手不发。
 */
static void test_stop_while_dragging_cancels(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 300U);
    snapshot.State = PLAYBACK_STATE_STOPPED;
    snapshot.StopReason = PLAYBACK_STOP_REASON_READ_ERROR;
    snapshot.PositionMs = 0U;
    snapshot.DurationMs = 0U;
    snapshot.StopCount = 1U;
    decision = reduce(&transport, &snapshot);
    assert(decision.seek_cancel == true);
    assert(decision.seek_enabled == false);
    assert(gui_music_transport_seek_release(&transport, 300U) == GUI_MUSIC_TRANSPORT_SEEK_NOT_SENT);
    assert(fake_send_calls == 0U);
}

/**
 * @brief 触摸丢失取消拖动：恢复按快照显示，之后松手不发。
 */
static void test_touch_lost_cancels_drag(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 300U);
    gui_music_transport_seek_cancel(&transport);
    decision = reduce(&transport, &snapshot);
    assert(decision.seek_dragging == false);
    assert(decision.position_ms == 40000U);
    assert(gui_music_transport_seek_release(&transport, 300U) == GUI_MUSIC_TRANSPORT_SEEK_NOT_SENT);
    assert(fake_send_calls == 0U);
}

/**
 * @brief 总时长未知（切歌后首帧前）时进度条不响应：按下、拖动无预览，松手不发；
 *        决定给出“不可跳转”。
 */
static void test_unknown_duration_ignores_press_drag_and_release(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 0U, 0U);

    decision = start(&transport, &snapshot);
    assert(decision.seek_enabled == false);

    gui_music_transport_seek_press(&transport, 500U);
    gui_music_transport_seek_drag(&transport, 600U);
    decision = reduce(&transport, &snapshot);
    assert(decision.seek_dragging == false);
    assert(decision.position_ms == 0U);
    assert(decision.duration_ms == 0U);
    assert(gui_music_transport_seek_release(&transport, 600U) == GUI_MUSIC_TRANSPORT_SEEK_NOT_SENT);
    assert(fake_send_calls == 0U);

    snapshot.DurationMs = 150000U;
    decision = reduce(&transport, &snapshot);
    assert(decision.seek_enabled == true);
}

/**
 * @brief 停止态（含锁存的 AUDIO_FAULT、总时长残留也不算）进度条不响应按压。
 */
static void test_stopped_state_is_not_seekable(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 0U, 200000U);

    snapshot.State = PLAYBACK_STATE_STOPPED;
    snapshot.StopReason = PLAYBACK_STOP_REASON_AUDIO_FAULT;
    decision = start(&transport, &snapshot);
    assert(decision.seek_enabled == false);
    gui_music_transport_seek_press(&transport, 500U);
    assert(gui_music_transport_seek_release(&transport, 500U) == GUI_MUSIC_TRANSPORT_SEEK_NOT_SENT);
    assert(fake_send_calls == 0U);
}

/**
 * @brief 按下绑定总时长：拖动中快照总时长变化（估算值修正），预览与目标仍按按下时的值。
 */
static void test_press_binds_duration_shown_at_press(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 40000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 500U);
    snapshot.DurationMs = 180000U;
    decision = reduce(&transport, &snapshot);
    assert(decision.position_ms == 100000U);
    assert(decision.duration_ms == 200000U);
    assert(gui_music_transport_seek_release(&transport, 500U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);
    assert(fake_last_command.TargetMs == 100000U);
}

/**
 * @brief 跳转不让唱盘归零：后退跳转、跳到 0 都不归零；暂停中跳转保持静止。
 */
static void test_seek_does_not_reset_vinyl(void)
{
    GUI_MusicTransportTypeDef transport;
    GUI_MusicTransportDecisionTypeDef decision;
    Playback_SnapshotTypeDef snapshot = make_playing(2U, 120000U, 200000U);

    (void)start(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 0U);
    assert(gui_music_transport_seek_release(&transport, 0U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);
    decision = reduce(&transport, &snapshot);
    assert(decision.vinyl_reset_angle == false);
    snapshot.HandledCommandSeq = 1U;
    snapshot.PositionMs = 0U;
    decision = reduce(&transport, &snapshot);
    assert(decision.vinyl_reset_angle == false);
    assert(decision.vinyl_playing == true);

    snapshot.State = PLAYBACK_STATE_PAUSED;
    (void)reduce(&transport, &snapshot);
    gui_music_transport_seek_press(&transport, 500U);
    assert(gui_music_transport_seek_release(&transport, 500U) == GUI_MUSIC_TRANSPORT_SEEK_SENT);
    snapshot.HandledCommandSeq = 2U;
    snapshot.PositionMs = 100000U;
    decision = reduce(&transport, &snapshot);
    assert(decision.state == PLAYBACK_STATE_PAUSED);
    assert(decision.vinyl_playing == false);
    assert(decision.vinyl_reset_angle == false);
    assert(decision.position_ms == 100000U);
}

/**
 * @brief 空指针不崩溃。
 */
static void test_seek_null_arguments_are_safe(void)
{
    fake_send_reset();
    gui_music_transport_seek_press(NULL, 0U);
    gui_music_transport_seek_drag(NULL, 0U);
    gui_music_transport_seek_cancel(NULL);
    assert(gui_music_transport_seek_release(NULL, 0U) == GUI_MUSIC_TRANSPORT_SEEK_NOT_SENT);
    assert(fake_send_calls == 0U);
}

/**
 * @brief 运行全部跳转归约测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_drag_previews_target_over_bound_duration();
    test_release_sends_seek_bound_to_pressed_track();
    test_release_at_both_ends();
    test_position_holds_target_until_echo_catches_up();
    test_consecutive_releases_hold_the_last_target();
    test_queue_full_drops_release_and_shows_snapshot();
    test_hold_applies_only_while_displayed_track_is_the_target_track();
    test_track_change_while_dragging_cancels_and_release_sends_nothing();
    test_stop_while_dragging_cancels();
    test_touch_lost_cancels_drag();
    test_unknown_duration_ignores_press_drag_and_release();
    test_stopped_state_is_not_seekable();
    test_press_binds_duration_shown_at_press();
    test_seek_does_not_reset_vinyl();
    test_seek_null_arguments_are_safe();

    puts("gui_music_seek_tests: all tests passed");
    return 0;
}
