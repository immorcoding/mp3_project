/**
  ******************************************************************************
  * @file    gui_music_transport.h
  * @brief   GUI Task 音乐分区私有的纯 C 归约：播放快照 -> 应显示什么。
  *
  * @details
  *          不依赖 LVGL、FreeRTOS 与 Service/gui；只包含播放任务公开合同头。
  *          本模块不持 playing、游标或假进度，三态与曲目身份的唯一真值是
  *          播放快照（#72 C1–C8、B5、B6）。归约结果由音乐分区编排与上次结果
  *          比较后，再决定调用哪些 Service_GUI_*。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_TRANSPORT_H
#define GUI_MUSIC_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/apps/music/gui/gui_music_queue_window.h"
#include "APP/tasks/playback/playback_contract.h"

/**
 * @brief 停止短语的历史状态（C6）。
 */
typedef enum
{
    GUI_MUSIC_TRANSPORT_PHRASE_NONE = 0U, /**< 采信的状态不是带原因的停止，无短语。 */
    GUI_MUSIC_TRANSPORT_PHRASE_SHOWN,     /**< 带原因的停止，短语常驻。 */
    GUI_MUSIC_TRANSPORT_PHRASE_DISMISSED  /**< 停止期间曲目身份变化，短语已清除，直到再次进入停止。 */
} GUI_MusicTransportPhraseModeTypeDef;

/** @brief 进度条手势比例的满量程（千分比）；本头不依赖 Service/gui，所以保留别名，由 gui_music.c 的 `_Static_assert` 与 `SERVICE_GUI_SEEK_SCALE` 绑定。 */
#define GUI_MUSIC_TRANSPORT_SEEK_SCALE  1000U

/**
 * @brief 松手的结果（GUI-10、GUI-17）。
 */
typedef enum
{
    GUI_MUSIC_TRANSPORT_SEEK_NOT_SENT = 0U, /**< 没有在途拖动（已取消、未按下或总时长未知），不发命令。 */
    GUI_MUSIC_TRANSPORT_SEEK_SENT,          /**< SEEK 已入队，位置保持目标直到回显追上。 */
    GUI_MUSIC_TRANSPORT_SEEK_QUEUE_FULL     /**< 命令队列满，本次输入丢弃，进度条回到快照。 */
} GUI_MusicTransportSeekResultTypeDef;

/**
 * @brief 曲目身份：（是否有当前曲, 代次, 位置）。
 */
typedef struct
{
    bool has_track;      /**< 是否有当前曲。 */
    uint32_t generation; /**< 曲库代次。 */
    uint16_t index;      /**< 播放列表位置。 */
} GUI_MusicTransportIdentityTypeDef;

/**
 * @brief 在途拖动：按下时绑定的曲目身份与总时长，以及当前比例。
 */
typedef struct
{
    bool active;                                /**< 正在拖动。 */
    GUI_MusicTransportIdentityTypeDef identity; /**< 按下时显示的曲目身份。 */
    uint32_t duration_ms;                       /**< 按下时显示的总时长（非 0）。 */
    uint16_t permille;                          /**< 当前比例 0..GUI_MUSIC_TRANSPORT_SEEK_SCALE。 */
} GUI_MusicTransportDragTypeDef;

/**
 * @brief 发送成功、回显尚未追上的跳转。
 */
typedef struct
{
    bool active;                                /**< 有在途跳转。 */
    uint32_t seq;                               /**< 这条 SEEK 的命令序号。 */
    uint32_t target_ms;                         /**< 目标毫秒。 */
    GUI_MusicTransportIdentityTypeDef identity; /**< 目标所属曲目。 */
} GUI_MusicTransportPendingSeekTypeDef;

/**
 * @brief 归约状态：命令序号、最后一份已追上的快照与各条规则的历史基准。
 * @note  字段只供 gui_music_transport.c 使用；调用方不直接读写。
 */
typedef struct
{
    uint32_t last_sent_seq;            /**< 最后一条发送成功的命令序号（B5）。 */
    bool has_accepted;                 /**< 已有一份已追上的快照（C1 冻结基准）。 */
    Playback_SnapshotTypeDef accepted; /**< 最后一份已追上的完整快照。 */
    bool has_previous;                 /**< 已归约过至少一份快照（C7 基线、C8 比较）。 */
    uint32_t skipped_baseline;         /**< 上次已计入的 SkippedBadCount（C7）。 */
    uint32_t stop_baseline;            /**< 上次已计入的 StopCount（C7）。 */
    Playback_StateTypeDef previous_state; /**< 上次采信的三态（GUI-20 判进入停止）。 */
    uint32_t previous_track_start_count; /**< 上次采信的 TrackStartCount（GUI-20）。 */
    uint32_t previous_duration_ms;     /**< 上次显示的总时长；按下时据此绑定（GUI-10）。 */
    bool previous_seek_enabled;        /**< 上次显示时总时长是否已知（进度条可响应按压）。 */
    GUI_MusicTransportDragTypeDef drag;           /**< 在途拖动（GUI-10）。 */
    GUI_MusicTransportPendingSeekTypeDef pending; /**< 在途跳转（GUI-17）。 */
    bool previous_has_track;           /**< 上次采信的曲目身份：是否有当前曲。 */
    uint32_t previous_generation;      /**< 上次采信的曲目身份：曲库代次。 */
    uint16_t previous_index;           /**< 上次采信的曲目身份：播放列表位置。 */
    GUI_MusicTransportPhraseModeTypeDef phrase_mode; /**< 停止短语状态（C6）。 */
    uint32_t phrase_stop_count;        /**< 进入这次停止时的 StopCount（C6 基准）。 */
    bool phrase_has_track;             /**< 进入这次停止那份快照的身份：是否有当前曲。 */
    uint32_t phrase_generation;        /**< 进入这次停止那份快照的身份：曲库代次。 */
    uint16_t phrase_index;             /**< 进入这次停止那份快照的身份：播放列表位置。 */
} GUI_MusicTransportTypeDef;

/**
 * @brief 一次归约得出的显示决定。
 */
typedef struct
{
    bool caught_up;                      /**< 回显已追上最后发出的命令（C1）。 */
    Playback_StateTypeDef state;         /**< 采信的三态；未追上时冻结。 */
    bool has_track;                      /**< 采信的曲目身份：是否有当前曲；未追上时冻结。 */
    uint32_t generation;                 /**< 采信的曲目身份：曲库代次；无当前曲时为 0。 */
    uint16_t index;                      /**< 采信的曲目身份：播放列表位置；无当前曲时为 0。 */
    bool highlight_apply;                /**< 是否应覆盖 Queue 高亮；未追上时为假（C3）。 */
    uint16_t highlight_index;            /**< 应高亮的播放列表位置；无高亮为 GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT。 */
    uint8_t progress_percent;            /**< 进度条百分比 0..100（C4）；拖动中为手势比例，在途跳转时为目标。 */
    uint32_t position_ms;                /**< 时间 Label 的时间：照用当前快照（C5）；拖动中为目标，在途跳转且曲目未变时为目标（GUI-17）。 */
    uint32_t duration_ms;                /**< 时间 Label 的总时长，0 = 未知；未追上时冻结（C5）；拖动中为按下时绑定的总时长。 */
    const char *status_phrase;           /**< 常驻停止短语；NULL = 显示时间（C6）。 */
    uint32_t skip_added;                 /**< 本次新增的跳过坏文件数，0 = 无（C7）。 */
    bool status_flash;                   /**< 本次应让状态行闪一次（C7）。 */
    bool vinyl_playing;                  /**< 唱盘是否转动（C8）。 */
    bool vinyl_reset_angle;              /**< 唱盘角度是否归零（GUI-20）。 */
    bool seek_enabled;                   /**< 进度条是否响应按压：总时长已知且不在停止态（GUI-10）。 */
    bool seek_dragging;                  /**< 正在拖动：时间与进度是预览值。 */
    bool seek_cancel;                    /**< 本次因显示的曲目身份变化或总时长变为未知而取消了拖动；一次性事件。 */
} GUI_MusicTransportDecisionTypeDef;

void gui_music_transport_init(GUI_MusicTransportTypeDef *transport);
Playback_StatusTypeDef gui_music_transport_send(
    GUI_MusicTransportTypeDef *transport,
    Playback_CommandKindTypeDef kind,
    uint32_t generation,
    uint16_t index);
void gui_music_transport_reduce(
    GUI_MusicTransportTypeDef *transport,
    const Playback_SnapshotTypeDef *snapshot,
    uint32_t window_generation,
    GUI_MusicTransportDecisionTypeDef *decision);
void gui_music_transport_seek_press(GUI_MusicTransportTypeDef *transport, uint16_t permille);
void gui_music_transport_seek_drag(GUI_MusicTransportTypeDef *transport, uint16_t permille);
GUI_MusicTransportSeekResultTypeDef gui_music_transport_seek_release(
    GUI_MusicTransportTypeDef *transport,
    uint16_t permille);
void gui_music_transport_seek_cancel(GUI_MusicTransportTypeDef *transport);
uint16_t gui_music_transport_withdraw_highlight(
    const GUI_MusicTransportTypeDef *transport,
    uint32_t window_generation);

#endif /* GUI_MUSIC_TRANSPORT_H */
