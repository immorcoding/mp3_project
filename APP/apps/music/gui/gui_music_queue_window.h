/**
  ******************************************************************************
  * @file    gui_music_queue_window.h
  * @brief   音乐分区私有的 Queue 窗口客户：写最新窗口需求、套用前台任务发布的窗口、空窗时清空 Queue。
  *
  * @details
  *          纯 C，不依赖 LVGL、FreeRTOS 与 Storage 头。GUI 不等文件、不查询卷状态：把最新的窗口起点（只留起点）写进需求格，
  *          前台任务整份发布窗口，客户读到新结果就套用——窗口不比需求序号，自带起点、代次和条数，又是整份拷贝，晚到也仍是
  *          列表上一段正确的内容（#200）。代次 0 的空窗（曲库不可用、代次变了、卷没了）清空 Queue。套用失败从 GUI 自己的窗口
  *          副本重试，不用重新要。下一窗起点让可见首行之上留 `GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS` 行给回滑（#157）。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_QUEUE_WINDOW_H
#define GUI_MUSIC_QUEUE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#define GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT  0xFFFFU  /* 无当前曲或代次不符；与 QueueApply 无当前曲同值。 */
#define GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS   3U       /* 非首窗在可见首行之上留给回滑的行数，须小于窗口容量；一屏约 5 行，余下的留在下方（#157）。 */

/** @brief 本帧对读到的新窗口结果的动作。 */
typedef enum
{
    GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE = 0U, /**< 本帧不套用、不清空。 */
    GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY,     /**< 套用 GUI 的窗口副本（新结果，或上次套用失败的重试）。 */
    GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR      /**< 收到代次 0 的空窗，应把 Queue 可见行清成空窗。 */
} GUI_MusicQueueWindowActionTypeDef;

typedef struct
{
    bool need_window;        /**< 还需要一窗：开机、滚动要新起点、空窗清空之后为真，套用成功后为假。 */
    bool displayed;          /**< 当前 Queue 展示着一窗；空窗清空后为假。 */
    uint16_t desired_index;  /**< 下一窗在播放列表上的起点。 */
    uint16_t applied_index;  /**< 已成功填进 Queue 的窗口起点。 */
    uint16_t applied_length; /**< 已成功填进 Queue 的本窗条数。 */
    uint32_t applied_generation; /**< 已成功填进 Queue 的本窗曲库代次；未显示为 0。 */
    bool written;            /**< 已经往需求格写过至少一次。 */
    uint16_t written_index;  /**< 最近一次写进需求格的起点；前台任务按最新需求答，新的覆盖旧的。 */
    bool apply_retry;        /**< 上次套用失败，下一帧从 GUI 的窗口副本重试。 */
} GUI_MusicQueueWindowClientTypeDef;

void gui_music_queue_window_client_init(GUI_MusicQueueWindowClientTypeDef *client);
void gui_music_queue_window_mark_applied(
    GUI_MusicQueueWindowClientTypeDef *client,
    uint16_t index,
    uint16_t length,
    uint32_t generation);
void gui_music_queue_window_apply_failed(GUI_MusicQueueWindowClientTypeDef *client);
uint32_t gui_music_queue_window_displayed_generation(
    const GUI_MusicQueueWindowClientTypeDef *client);
uint16_t gui_music_queue_window_desired_index(
    uint16_t applied_index,
    uint16_t applied_length,
    uint16_t lead,
    uint16_t max_entries);
void gui_music_queue_window_note_lead(
    GUI_MusicQueueWindowClientTypeDef *client,
    uint16_t lead,
    uint16_t max_entries);
uint16_t gui_music_queue_window_request_index(
    const GUI_MusicQueueWindowClientTypeDef *client);
bool gui_music_queue_window_request_due(const GUI_MusicQueueWindowClientTypeDef *client);
void gui_music_queue_window_request_sent(GUI_MusicQueueWindowClientTypeDef *client);
GUI_MusicQueueWindowActionTypeDef gui_music_queue_window_poll(
    GUI_MusicQueueWindowClientTypeDef *client,
    bool new_result,
    uint32_t result_generation);

#endif /* GUI_MUSIC_QUEUE_WINDOW_H */
