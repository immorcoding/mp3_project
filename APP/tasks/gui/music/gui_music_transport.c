/**
  ******************************************************************************
  * @file    gui_music_transport.c
  * @brief   音乐分区私有的纯 C 归约：播放快照 -> 应显示什么。
  ******************************************************************************
  */

#include "gui_music_transport.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief 短语表长度：停止原因个数，ALL_BAD 须是枚举最后一项。
 * @note  新增原因（含在 ALL_BAD 之后）时必须同步改本值和短语表，否则下面的断言使编译失败。
 */
#define GUI_MUSIC_TRANSPORT_STOP_REASON_COUNT 10U

_Static_assert(GUI_MUSIC_TRANSPORT_STOP_REASON_COUNT == (PLAYBACK_STOP_REASON_ALL_BAD + 1U),
               "stop reason count changed: update the phrase table and the count");

/** 停止原因 -> 状态行短语（C6）；NONE 为空，状态行回到时间。只用 Montserrat 拉丁字形。 */
static const char *const gui_music_transport_phrase_table[GUI_MUSIC_TRANSPORT_STOP_REASON_COUNT] =
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
 * @brief 给命令编号并发送，只有发送成功才记为最后发出（B5）。
 * @param[in,out] transport 本分区持有的归约状态。
 * @param[in] kind 命令种类。
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
    Playback_StatusTypeDef status;

    if (transport == NULL)
    {
        return PLAYBACK_QUEUE_FULL;
    }

    command.Kind = kind;
    command.Seq = transport->last_sent_seq + 1U;
    command.Generation = generation;
    command.Index = index;
    status = playback_command_send(&command);
    if (status == PLAYBACK_OK)
    {
        transport->last_sent_seq = command.Seq;
    }

    return status;
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
        (adopted->StopReason > PLAYBACK_STOP_REASON_ALL_BAD))
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

    /* 唱盘（C8）：切歌、同曲重选、停止归零都表现为身份变化或位置变小。 */
    decision->vinyl_playing = (adopted.State == PLAYBACK_STATE_PLAYING);
    if (transport->has_previous)
    {
        decision->vinyl_reset_angle =
            !gui_music_transport_same_identity(
                &adopted,
                transport->previous_has_track,
                transport->previous_generation,
                transport->previous_index) ||
            (adopted.PositionMs < transport->previous_position_ms);
    }
    transport->previous_has_track = adopted.HasTrack;
    transport->previous_generation = adopted.Generation;
    transport->previous_index = adopted.Index;
    transport->previous_position_ms = adopted.PositionMs;
    transport->has_previous = true;

    decision->highlight_apply = caught_up;
    decision->highlight_index = gui_music_transport_highlight_of(&adopted, window_generation);
    decision->state = adopted.State;
    decision->status_phrase = gui_music_transport_phrase_step(transport, &adopted);
    decision->position_ms = adopted.PositionMs;
    decision->duration_ms = adopted.DurationMs;
    decision->progress_percent = gui_music_transport_progress_percent(
        adopted.PositionMs,
        adopted.DurationMs);
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
