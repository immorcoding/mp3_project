/**
  ******************************************************************************
  * @file    gui_music_transport.c
  * @brief   音乐分区私有的纯 C 归约：播放快照 -> 应显示什么。
  ******************************************************************************
  */

#include "gui_music_transport.h"

#include <stddef.h>
#include <string.h>

/* 短语表长度取合同的原因个数；ALL_BAD 之后又有新原因时，下面的断言使编译失败，提醒补短语。 */
_Static_assert((unsigned)PLAYBACK_STOP_REASON_ALL_BAD + 1U == (unsigned)PLAYBACK_STOP_REASON_COUNT,
               "ALL_BAD is no longer the last stop reason: add the new phrase and update this assert");

/** 停止原因 -> 状态行短语（C6）；NONE 为空，状态行回到时间。只用 Montserrat 拉丁字形。 */
static const char *const gui_music_transport_phrase_table[PLAYBACK_STOP_REASON_COUNT] =
{
    [PLAYBACK_STOP_REASON_NONE] = NULL,
    [PLAYBACK_STOP_REASON_CARD_REMOVED] = "No SD card",
    [PLAYBACK_STOP_REASON_NOT_READY] = "SD card not ready",
    [PLAYBACK_STOP_REASON_NO_TRACK] = "No tracks",
    [PLAYBACK_STOP_REASON_CATALOG_CHANGED] = "Library changed",
    [PLAYBACK_STOP_REASON_READ_ERROR] = "Read error",
    [PLAYBACK_STOP_REASON_NO_DATA] = "Read timeout",
    [PLAYBACK_STOP_REASON_AUDIO_ERROR] = "Audio error",
    [PLAYBACK_STOP_REASON_AUDIO_FAULT] = "Audio fault, restart",
    [PLAYBACK_STOP_REASON_ALL_BAD] = "No playable files"
};

/**
 * @brief 复位归约状态。
 * @param[out] transport 本分区持有的归约状态；NULL 忽略。
 */
void gui_music_transport_init(GUI_MusicTransportTypeDef *transport)
{
    if (transport == NULL)
    {
        return;
    }

    (void)memset(transport, 0, sizeof(*transport));
}

/**
 * @brief 给一条完整命令编号并发送，只有发送成功才记为最后发出（B5）。
 * @param[in,out] transport 本分区持有的归约状态，调用方已确认非 NULL。
 * @param[in,out] command 待发送的命令；本函数填 Seq。
 * @return 播放合同返回值。
 */
static Playback_StatusTypeDef gui_music_transport_send_command(
    GUI_MusicTransportTypeDef *transport,
    Playback_CommandTypeDef *command)
{
    Playback_StatusTypeDef status;

    command->Seq = transport->last_sent_seq + 1U;
    status = playback_command_send(command);
    if (status == PLAYBACK_OK)
    {
        transport->last_sent_seq = command->Seq;
    }

    return status;
}

/**
 * @brief 给命令编号并发送，只有发送成功才记为最后发出（B5）。
 * @param[in,out] transport 本分区持有的归约状态。
 * @param[in] kind 命令种类（SEEK 走 gui_music_transport_seek_release）。
 * @param[in] generation 仅 SELECT：已显示窗口的曲库代次。
 * @param[in] index 仅 SELECT：播放列表位置。
 * @return 播放合同返回值；NULL 状态按队列满处理且不发送。
 */
Playback_StatusTypeDef gui_music_transport_send(
    GUI_MusicTransportTypeDef *transport,
    Playback_CommandKindTypeDef kind,
    uint32_t generation,
    uint16_t index)
{
    Playback_CommandTypeDef command;

    if (transport == NULL)
    {
        return PLAYBACK_QUEUE_FULL;
    }

    (void)memset(&command, 0, sizeof(command));
    command.Kind = kind;
    command.Generation = generation;
    command.Index = index;
    return gui_music_transport_send_command(transport, &command);
}

/**
 * @brief 按一份快照的曲目身份算 Queue 高亮（C3 后三条）。
 * @param[in] snapshot 参与判断的快照。
 * @param[in] window_generation 已显示 Queue 窗口的曲库代次。
 * @return 有当前曲、代次非 0 且等于窗口代次时为其位置，否则
 *         GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT。
 */
static uint16_t gui_music_transport_highlight_of(
    const Playback_SnapshotTypeDef *snapshot,
    uint32_t window_generation)
{
    if (!snapshot->HasTrack ||
        (snapshot->Generation == 0U) ||
        (snapshot->Generation != window_generation))
    {
        return GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT;
    }

    return snapshot->Index;
}

/**
 * @brief 判断一份快照的曲目身份是否等于记下的身份。
 * @param[in] snapshot 当前采信的快照。
 * @param[in] has_track 记下的身份：是否有当前曲。
 * @param[in] generation 记下的身份：曲库代次。
 * @param[in] index 记下的身份：播放列表位置。
 * @return 无当前曲时只比较“无”；否则代次与位置都相等才为真。
 */
static bool gui_music_transport_same_identity(
    const Playback_SnapshotTypeDef *snapshot,
    bool has_track,
    uint32_t generation,
    uint16_t index)
{
    if (snapshot->HasTrack != has_track)
    {
        return false;
    }

    return !has_track ||
           ((snapshot->Generation == generation) && (snapshot->Index == index));
}

/**
 * @brief 判断一份快照的曲目身份是否等于记下的身份结构。
 * @param[in] snapshot 当前采信的快照。
 * @param[in] identity 记下的身份。
 * @return 同 gui_music_transport_same_identity。
 */
static bool gui_music_transport_is_identity(
    const Playback_SnapshotTypeDef *snapshot,
    const GUI_MusicTransportIdentityTypeDef *identity)
{
    return gui_music_transport_same_identity(
        snapshot,
        identity->has_track,
        identity->generation,
        identity->index);
}

/**
 * @brief 判断采信的快照能否跳转：有当前曲、不在停止态且总时长已知（GUI-10）。
 * @param[in] adopted 采信的快照。
 * @return 为真时进度条响应按压。
 */
static bool gui_music_transport_seekable(const Playback_SnapshotTypeDef *adopted)
{
    return adopted->HasTrack &&
           (adopted->State != PLAYBACK_STATE_STOPPED) &&
           (adopted->DurationMs != 0U);
}

/**
 * @brief 把手势比例夹到 0..GUI_MUSIC_TRANSPORT_SEEK_SCALE。
 * @param[in] permille 手势比例。
 * @return 夹过的比例。
 */
static uint16_t gui_music_transport_clamp_permille(uint16_t permille)
{
    return (permille > GUI_MUSIC_TRANSPORT_SEEK_SCALE) ?
           (uint16_t)GUI_MUSIC_TRANSPORT_SEEK_SCALE : permille;
}

/**
 * @brief 比例 × 总时长，按 64 位计算后向下取整。
 * @param[in] permille 比例 0..GUI_MUSIC_TRANSPORT_SEEK_SCALE。
 * @param[in] duration_ms 总时长。
 * @return 目标毫秒。
 */
static uint32_t gui_music_transport_target_ms(uint16_t permille, uint32_t duration_ms)
{
    return (uint32_t)(((uint64_t)permille * duration_ms) / GUI_MUSIC_TRANSPORT_SEEK_SCALE);
}

/**
 * @brief 推进停止短语状态并返回应常驻的短语（C6）。
 * @param[in,out] transport 本分区持有的归约状态。
 * @param[in] adopted 采信的快照（冻结身份与三态，计数为当前值）。
 * @return 应常驻的短语；NULL 表示状态行显示时间。
 * @note  以进入这次停止（StopCount 变化）那份快照的身份为基准；StopCount
 *        不变时身份再变才清除，之后保持清除直到离开停止或再次进入停止。
 */
static const char *gui_music_transport_phrase_step(
    GUI_MusicTransportTypeDef *transport,
    const Playback_SnapshotTypeDef *adopted)
{
    if ((adopted->State != PLAYBACK_STATE_STOPPED) ||
        (adopted->StopReason == PLAYBACK_STOP_REASON_NONE) ||
        ((uint32_t)adopted->StopReason >= (uint32_t)PLAYBACK_STOP_REASON_COUNT))
    {
        transport->phrase_mode = GUI_MUSIC_TRANSPORT_PHRASE_NONE;
        return NULL;
    }

    if ((transport->phrase_mode == GUI_MUSIC_TRANSPORT_PHRASE_NONE) ||
        (adopted->StopCount != transport->phrase_stop_count))
    {
        transport->phrase_mode = GUI_MUSIC_TRANSPORT_PHRASE_SHOWN;
        transport->phrase_stop_count = adopted->StopCount;
        transport->phrase_has_track = adopted->HasTrack;
        transport->phrase_generation = adopted->Generation;
        transport->phrase_index = adopted->Index;
    }
    else if (!gui_music_transport_same_identity(
                 adopted,
                 transport->phrase_has_track,
                 transport->phrase_generation,
                 transport->phrase_index))
    {
        transport->phrase_mode = GUI_MUSIC_TRANSPORT_PHRASE_DISMISSED;
    }

    if (transport->phrase_mode != GUI_MUSIC_TRANSPORT_PHRASE_SHOWN)
    {
        return NULL;
    }

    return gui_music_transport_phrase_table[adopted->StopReason];
}

/**
 * @brief 按已解码时间与总时长算进度百分比（C4）。
 * @param[in] position_ms 已解码时间。
 * @param[in] duration_ms 总时长，0 = 未知。
 * @return 0..100；总时长未知为 0，位置超过总时长钳在 100。
 */
static uint8_t gui_music_transport_progress_percent(
    uint32_t position_ms,
    uint32_t duration_ms)
{
    uint64_t clamped;

    if (duration_ms == 0U)
    {
        return 0U;
    }

    clamped = (position_ms < duration_ms) ? position_ms : duration_ms;
    return (uint8_t)((clamped * 100ULL) / duration_ms);
}

/**
 * @brief 归约一份快照。
 * @param[in,out] transport 本分区持有的归约状态。
 * @param[in] snapshot 本圈读到的播放快照。
 * @param[in] window_generation 已显示 Queue 窗口的曲库代次；未显示为 0。
 * @param[out] decision 应显示的内容；任一指针为 NULL 时输出全零（无高亮、不覆盖）。
 * @note  冻结规则（C1）：回显未追上时，曲目身份、三态、停止原因与总时长取最后一份
 *        已追上的快照，位置与两个计数照用本份快照；计数提示以首份快照为基线（C7）。
 */
void gui_music_transport_reduce(
    GUI_MusicTransportTypeDef *transport,
    const Playback_SnapshotTypeDef *snapshot,
    uint32_t window_generation,
    GUI_MusicTransportDecisionTypeDef *decision)
{
    Playback_SnapshotTypeDef adopted;
    bool caught_up;

    if (decision == NULL)
    {
        return;
    }

    (void)memset(decision, 0, sizeof(*decision));
    if ((transport == NULL) || (snapshot == NULL))
    {
        decision->highlight_index = GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT;
        return;
    }

    /* 有符号差：序号回绕后仍能判断先后（B5）。 */
    caught_up = ((int32_t)(snapshot->HandledCommandSeq - transport->last_sent_seq) >= 0);
    if (caught_up || !transport->has_accepted)
    {
        transport->accepted = *snapshot;
        transport->has_accepted = true;
    }

    /* 冻结（C1）：身份、三态、停止原因与总时长取最后一份已追上的快照，位置与计数照用当前。 */
    adopted = transport->accepted;
    adopted.PositionMs = snapshot->PositionMs;
    adopted.SkippedBadCount = snapshot->SkippedBadCount;
    adopted.StopCount = snapshot->StopCount;

    decision->caught_up = caught_up;
    if (transport->has_previous)
    {
        /* 单调计数的无符号差；同一圈多次停止只闪一次（C7）。 */
        decision->skip_added = snapshot->SkippedBadCount - transport->skipped_baseline;
        decision->status_flash = (snapshot->StopCount != transport->stop_baseline);
    }
    transport->skipped_baseline = snapshot->SkippedBadCount;
    transport->stop_baseline = snapshot->StopCount;

    /* 在途跳转（GUI-17）：回显追上它的序号即结束保持。 */
    if (transport->pending.active &&
        ((int32_t)(snapshot->HandledCommandSeq - transport->pending.seq) >= 0))
    {
        transport->pending.active = false;
    }

    /* 拖动（GUI-10）：显示的曲目身份变化或总时长变为未知即取消，不发命令。 */
    if (transport->drag.active &&
        (!gui_music_transport_is_identity(&adopted, &transport->drag.identity) ||
         !gui_music_transport_seekable(&adopted)))
    {
        transport->drag.active = false;
        decision->seek_cancel = true;
    }

    /* 唱盘（GUI-20）：曲目身份变化、TrackStartCount 变化或进入停止时归零；跳转不归零。 */
    decision->vinyl_playing = (adopted.State == PLAYBACK_STATE_PLAYING);
    if (transport->has_previous)
    {
        decision->vinyl_reset_angle =
            !gui_music_transport_same_identity(
                &adopted,
                transport->previous_has_track,
                transport->previous_generation,
                transport->previous_index) ||
            (adopted.TrackStartCount != transport->previous_track_start_count) ||
            ((adopted.State == PLAYBACK_STATE_STOPPED) &&
             (transport->previous_state != PLAYBACK_STATE_STOPPED));
    }
    transport->previous_has_track = adopted.HasTrack;
    transport->previous_generation = adopted.Generation;
    transport->previous_index = adopted.Index;
    transport->previous_state = adopted.State;
    transport->previous_track_start_count = adopted.TrackStartCount;
    transport->previous_duration_ms = adopted.DurationMs;
    transport->previous_seek_enabled = gui_music_transport_seekable(&adopted);
    transport->has_previous = true;

    decision->highlight_apply = caught_up;
    decision->highlight_index = gui_music_transport_highlight_of(&adopted, window_generation);
    decision->state = adopted.State;
    decision->has_track = adopted.HasTrack;
    decision->generation = adopted.HasTrack ? adopted.Generation : 0U;
    decision->index = adopted.HasTrack ? adopted.Index : 0U;
    decision->status_phrase = gui_music_transport_phrase_step(transport, &adopted);
    decision->seek_enabled = transport->previous_seek_enabled;

    if (transport->drag.active)
    {
        /* 拖动中预览“目标/按下时的总时长”，进度跟手（GUI-10）。 */
        decision->seek_dragging = true;
        decision->position_ms = gui_music_transport_target_ms(
            transport->drag.permille,
            transport->drag.duration_ms);
        decision->duration_ms = transport->drag.duration_ms;
        decision->progress_percent = (uint8_t)(transport->drag.permille /
                                               (GUI_MUSIC_TRANSPORT_SEEK_SCALE / 100U));
        return;
    }

    decision->position_ms = adopted.PositionMs;
    if (transport->pending.active &&
        transport->previous_seek_enabled &&
        gui_music_transport_is_identity(&adopted, &transport->pending.identity))
    {
        /* 跳转在途、显示的曲目仍是目标所属曲目：位置显示目标（GUI-17）。 */
        decision->position_ms = transport->pending.target_ms;
    }
    decision->duration_ms = adopted.DurationMs;
    decision->progress_percent = gui_music_transport_progress_percent(
        decision->position_ms,
        adopted.DurationMs);
}

/**
 * @brief 进度条按下：总时长已知时开始拖动，绑定此刻显示的曲目身份与总时长（GUI-10）。
 * @param[in,out] transport 本分区持有的归约状态；NULL 忽略。
 * @param[in] permille 按下处的比例 0..GUI_MUSIC_TRANSPORT_SEEK_SCALE，超出夹到满量程。
 * @note  “显示的”是上一次归约的结果；总时长未知（含停止态）时什么也不做。
 */
void gui_music_transport_seek_press(GUI_MusicTransportTypeDef *transport, uint16_t permille)
{
    if ((transport == NULL) || !transport->has_previous || !transport->previous_seek_enabled)
    {
        return;
    }

    transport->drag.active = true;
    transport->drag.identity.has_track = transport->previous_has_track;
    transport->drag.identity.generation = transport->previous_generation;
    transport->drag.identity.index = transport->previous_index;
    transport->drag.duration_ms = transport->previous_duration_ms;
    transport->drag.permille = gui_music_transport_clamp_permille(permille);
}

/**
 * @brief 进度条拖动：更新在途拖动的比例；没有在途拖动时忽略。
 * @param[in,out] transport 本分区持有的归约状态；NULL 忽略。
 * @param[in] permille 当前比例，超出夹到满量程。
 */
void gui_music_transport_seek_drag(GUI_MusicTransportTypeDef *transport, uint16_t permille)
{
    if ((transport == NULL) || !transport->drag.active)
    {
        return;
    }

    transport->drag.permille = gui_music_transport_clamp_permille(permille);
}

/**
 * @brief 进度条松手：有在途拖动时发 SEEK{按下时的曲目身份, 比例 × 按下时的总时长}。
 * @param[in,out] transport 本分区持有的归约状态。
 * @param[in] permille 松手处的比例，超出夹到满量程。
 * @return 是否发出；发送成功后位置保持目标直到回显追上（GUI-17），队列满丢弃本次输入。
 * @note  每次松手都发、不节流；连续松手只保持最后一次的目标。
 */
GUI_MusicTransportSeekResultTypeDef gui_music_transport_seek_release(
    GUI_MusicTransportTypeDef *transport,
    uint16_t permille)
{
    Playback_CommandTypeDef command;
    uint32_t target_ms;

    if ((transport == NULL) || !transport->drag.active)
    {
        return GUI_MUSIC_TRANSPORT_SEEK_NOT_SENT;
    }

    transport->drag.active = false;
    target_ms = gui_music_transport_target_ms(
        gui_music_transport_clamp_permille(permille),
        transport->drag.duration_ms);

    (void)memset(&command, 0, sizeof(command));
    command.Kind = PLAYBACK_COMMAND_SEEK;
    command.Generation = transport->drag.identity.generation;
    command.Index = transport->drag.identity.index;
    command.TargetMs = target_ms;
    if (gui_music_transport_send_command(transport, &command) != PLAYBACK_OK)
    {
        return GUI_MUSIC_TRANSPORT_SEEK_QUEUE_FULL;
    }

    transport->pending.active = true;
    transport->pending.seq = command.Seq;
    transport->pending.target_ms = target_ms;
    transport->pending.identity = transport->drag.identity;
    return GUI_MUSIC_TRANSPORT_SEEK_SENT;
}

/**
 * @brief 进度条触摸丢失：结束在途拖动，不发命令。
 * @param[in,out] transport 本分区持有的归约状态；NULL 忽略。
 */
void gui_music_transport_seek_cancel(GUI_MusicTransportTypeDef *transport)
{
    if (transport == NULL)
    {
        return;
    }

    transport->drag.active = false;
}

/**
 * @brief 按最后一份已追上的快照算高亮，供队列满撤回即时高亮（B6）。
 * @param[in] transport 本分区持有的归约状态。
 * @param[in] window_generation 已显示 Queue 窗口的曲库代次。
 * @return 应高亮的播放列表位置；无高亮为 GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT。
 */
uint16_t gui_music_transport_withdraw_highlight(
    const GUI_MusicTransportTypeDef *transport,
    uint32_t window_generation)
{
    if ((transport == NULL) || !transport->has_accepted)
    {
        return GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT;
    }

    return gui_music_transport_highlight_of(&transport->accepted, window_generation);
}
