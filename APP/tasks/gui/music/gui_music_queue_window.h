/**
  ******************************************************************************
  * @file    gui_music_queue_window.h
  * @brief   音乐分区私有的 listbuffer 窗口请求/消费状态机。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_QUEUE_WINDOW_H
#define GUI_MUSIC_QUEUE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#define GUI_MUSIC_QUEUE_WINDOW_IDLE    0U  /* 与 STORAGE_LISTBUFFER_IDLE 同值。 */
#define GUI_MUSIC_QUEUE_WINDOW_PENDING 1U  /* 与 STORAGE_LISTBUFFER_PENDING 同值。 */
#define GUI_MUSIC_QUEUE_WINDOW_READY   2U  /* 与 STORAGE_LISTBUFFER_READY 同值。 */
#define GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT  0xFFFFU  /* 无当前曲或代次不符；与 QueueApply 无当前曲同值。 */
#define GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS   3U       /* 非首窗在可见首行之上留给回滑的行数，须小于窗口容量；一屏约 5 行，余下的留在下方（#157）。 */

typedef enum
{
    GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE = 0U, /**< 本轮不发起 request，也不填行。 */
    GUI_MUSIC_QUEUE_WINDOW_ACTION_REQUEST,   /**< SD 就绪且槽位 IDLE，应提交一窗。 */
    GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY,     /**< 槽位 READY，应整窗填行并写回 IDLE。 */
    GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR      /**< 卷已卸载，应把 Queue 可见行清成空窗。 */
} GUI_MusicQueueWindowActionTypeDef;

typedef struct
{
    bool need_window;        /**< 下一次 IDLE 且 SD 就绪时应 request。 */
    bool displayed;          /**< 当前 Queue 仍展示上一窗，卸载后需要 CLEAR。 */
    uint16_t desired_index;  /**< 下一窗在播放列表上的起点。 */
    uint16_t applied_index;  /**< 已成功填进 Queue 的窗口起点。 */
    uint16_t applied_length; /**< 已成功填进 Queue 的本窗条数。 */
    uint32_t applied_generation; /**< 已成功填进 Queue 的本窗曲库代次；未显示为 0。 */
} GUI_MusicQueueWindowClientTypeDef;

void gui_music_queue_window_client_init(GUI_MusicQueueWindowClientTypeDef *client);
void gui_music_queue_window_mark_applied(
    GUI_MusicQueueWindowClientTypeDef *client,
    uint16_t index,
    uint16_t length,
    uint32_t generation);
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
GUI_MusicQueueWindowActionTypeDef gui_music_queue_window_poll(
    GUI_MusicQueueWindowClientTypeDef *client,
    bool sd_ready,
    bool sd_mounted,
    uint32_t status);

#endif /* GUI_MUSIC_QUEUE_WINDOW_H */
