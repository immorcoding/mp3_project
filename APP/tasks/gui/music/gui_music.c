/**
  ******************************************************************************
  * @file    gui_music.c
  * @brief   音乐分区：消费本分区输入、窗口协议、playing 与假进度。
  *
  * @details
  *          不打开文件、不解码。Playback 打开/预开仍只留注释。游标与
  *          listbuffer 的头文件只出现在本分区。
  ******************************************************************************
  */

#include "gui_music.h"
#include "gui_music_queue_window.h"
#include "gui_music_transport.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/storage/catalog/storage_listbuffer.h"
#include "APP/tasks/storage/catalog/storage_playback_cursor.h"

static GUI_MusicQueueWindowClientTypeDef gui_music_queue_window_client;
static GUI_MusicTransportTypeDef gui_music_transport;

/**
 * @brief 把 READY 窗口填进 Queue。
 * @return SERVICE_OK 已按 Length 填行或空窗。
 * @note  卷已卸载时不读 Buffer，填空窗以免画出已拔卡路径。当前 Buffer 仍是
 *        曲库路径。当前行由播放列表游标判定，须与窗口代次一致。
 */
static Service_StatusTypeDef gui_music_apply_ready_window(void)
{
    const char *titles[STORAGE_LISTBUFFER_MAX_ENTRIES];
    uint16_t length;
    uint16_t i;
    uint16_t cursor_index = 0U;
    uint32_t cursor_generation = 0U;
    uint16_t current_index;
    bool cursor_valid;

    if (!storage_task_sd_is_mounted())
    {
        return Service_GUI_QueueApply(NULL, 0U, 0U, SERVICE_GUI_QUEUE_NO_CURRENT);
    }

    length = storage_listbuffer.Length;
    if (length > STORAGE_LISTBUFFER_MAX_ENTRIES)
    {
        length = STORAGE_LISTBUFFER_MAX_ENTRIES;
    }

    for (i = 0U; i < length; i++)
    {
        titles[i] = storage_listbuffer.Buffer[i];
    }

    cursor_valid = (storage_playback_cursor_get(&cursor_index, &cursor_generation) ==
                    STORAGE_OK);
    current_index = gui_music_queue_window_current_index(
        cursor_valid,
        cursor_index,
        cursor_generation,
        storage_listbuffer.Generation);

    if (length == 0U)
    {
        return Service_GUI_QueueApply(NULL, 0U, 0U, current_index);
    }

    return Service_GUI_QueueApply(
        titles,
        length,
        storage_listbuffer.Index,
        current_index);
}

/**
 * @brief 处理本分区输入命令。
 * @param[in] input 本圈命令；NULL 视为 NONE。
 * @param[out] cursor_moved 游标是否被本圈命令改动。
 * @param[out] playing_changed playing 标志是否被本圈命令改动。
 * @param[out] progress_changed 假进度是否需要写回进度条。
 */
static void gui_music_dispatch_input(
    const Service_GUI_InputTypeDef *input,
    bool *cursor_moved,
    bool *playing_changed,
    bool *progress_changed)
{
    Service_GUI_InputCommandTypeDef command;
    bool has_current;
    uint16_t cursor_index;
    uint32_t cursor_generation;

    *cursor_moved = false;
    *playing_changed = false;
    *progress_changed = false;
    command = (input == NULL) ? SERVICE_GUI_INPUT_NONE : input->command;

    switch (command)
    {
        case SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT:
            if (storage_playback_cursor_set(input->param) == STORAGE_OK)
            {
                *cursor_moved = true;
                (void)gui_music_transport_reset_progress(&gui_music_transport);
                *progress_changed = true;
            }
            /*
             * Playback Task（未落地）
             * ------------------------------------------------------------
             * 游标已改。此处应通知后台播放进程打开/预开该位置，不要在
             * GUI Task 里读文件或解码。
             * ------------------------------------------------------------
             */
            break;

        case SERVICE_GUI_INPUT_MUSIC_PREVIOUS:
            if (storage_playback_cursor_previous() == STORAGE_OK)
            {
                *cursor_moved = true;
                (void)gui_music_transport_reset_progress(&gui_music_transport);
                *progress_changed = true;
            }
            break;

        case SERVICE_GUI_INPUT_MUSIC_NEXT:
            if (storage_playback_cursor_next() == STORAGE_OK)
            {
                *cursor_moved = true;
                (void)gui_music_transport_reset_progress(&gui_music_transport);
                *progress_changed = true;
            }
            break;

        case SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE:
            has_current = (storage_playback_cursor_get(
                               &cursor_index,
                               &cursor_generation) == STORAGE_OK);
            *playing_changed = gui_music_transport_toggle(
                &gui_music_transport,
                has_current);
            break;

        case SERVICE_GUI_INPUT_MUSIC_SEEK:
            has_current = (storage_playback_cursor_get(
                               &cursor_index,
                               &cursor_generation) == STORAGE_OK);
            if (has_current)
            {
                uint8_t percent;

                percent = (input->param > 100U) ?
                          100U :
                          (uint8_t)input->param;
                (void)gui_music_transport_set_progress(
                    &gui_music_transport,
                    percent);
            }
            *progress_changed = true;
            break;

        case SERVICE_GUI_INPUT_NONE:
        default:
            break;
    }
}

/**
 * @brief 复位窗口客户、playing 与假进度，并刷新图标和进度条。
 */
void gui_music_init(void)
{
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_IDLE == STORAGE_LISTBUFFER_IDLE,
                   "music queue window IDLE must match listbuffer");
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_PENDING == STORAGE_LISTBUFFER_PENDING,
                   "music queue window PENDING must match listbuffer");
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_READY == STORAGE_LISTBUFFER_READY,
                   "music queue window READY must match listbuffer");
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT == SERVICE_GUI_QUEUE_NO_CURRENT,
                   "music queue window no-current must match QueueApply sentinel");

    gui_music_queue_window_client_init(&gui_music_queue_window_client);
    gui_music_transport_init(&gui_music_transport);
    (void)Service_GUI_TransportApply(false);
    (void)Service_GUI_ProgressApply(0U);
    (void)Service_GUI_VinylApply(false, true);
}

/**
 * @brief 推进音乐分区一步：命令、窗口协议、按需刷新高亮、图标与进度条。
 * @param[in] input 本圈已 Consume 的输入；NULL 视为 NONE。
 */
void gui_music_step(const Service_GUI_InputTypeDef *input)
{
    GUI_MusicQueueWindowActionTypeDef action;
    bool cursor_moved;
    bool playing_changed;
    bool progress_changed;
    bool vinyl_reset;
    bool displayed_window_idle;

    gui_music_dispatch_input(
        input,
        &cursor_moved,
        &playing_changed,
        &progress_changed);
    /* 切歌（游标变化）让唱盘归零；清空播放内容在 CLEAR 分支再置真。 */
    vinyl_reset = cursor_moved;

    gui_music_queue_window_note_lead(
        &gui_music_queue_window_client,
        Service_GUI_QueueScrollLead(),
        STORAGE_LISTBUFFER_MAX_ENTRIES);

    action = gui_music_queue_window_poll(
        &gui_music_queue_window_client,
        storage_task_sd_is_ready(),
        storage_task_sd_is_mounted(),
        storage_listbuffer.Status);

    switch (action)
    {
        case GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE:
            break;

        case GUI_MUSIC_QUEUE_WINDOW_ACTION_REQUEST:
            (void)storage_listbuffer_request(
                gui_music_queue_window_request_index(
                    &gui_music_queue_window_client),
                STORAGE_LISTBUFFER_MAX_ENTRIES,
                0U);
            break;

        case GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY:
            if ((gui_music_apply_ready_window() == SERVICE_OK) &&
                storage_task_sd_is_mounted())
            {
                uint16_t applied_index;
                uint16_t applied_length;

                applied_length = storage_listbuffer.Length;
                applied_index = storage_listbuffer.Index;
                if (applied_length == 0U)
                {
                    applied_index = 0U;
                }
                else if (applied_length > STORAGE_LISTBUFFER_MAX_ENTRIES)
                {
                    applied_length = STORAGE_LISTBUFFER_MAX_ENTRIES;
                }

                gui_music_queue_window_mark_applied(
                    &gui_music_queue_window_client,
                    applied_index,
                    applied_length);
            }

            storage_listbuffer.Status = STORAGE_LISTBUFFER_IDLE;
            break;

        case GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR:
            (void)Service_GUI_QueueApply(NULL, 0U, 0U, SERVICE_GUI_QUEUE_NO_CURRENT);
            if (gui_music_transport_force_paused(&gui_music_transport))
            {
                playing_changed = true;
            }
            (void)gui_music_transport_reset_progress(&gui_music_transport);
            progress_changed = true;
            vinyl_reset = true;
            break;

        default:
            break;
    }

    displayed_window_idle =
        gui_music_queue_window_client.displayed &&
        (storage_listbuffer.Status == STORAGE_LISTBUFFER_IDLE) &&
        storage_task_sd_is_mounted();

    if (cursor_moved && displayed_window_idle &&
        (action != GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY))
    {
        (void)gui_music_apply_ready_window();
    }

    if (playing_changed)
    {
        (void)Service_GUI_TransportApply(
            gui_music_transport_is_playing(&gui_music_transport));
    }

    /*
     * 唱盘只在 playing 变化、切歌或清空时重新对齐：playing 起转/续转，paused 停在
     * 当前角度；切歌与清空先归零再按 playing 决定。拖进度条不碰唱盘。
     */
    if (playing_changed || vinyl_reset)
    {
        (void)Service_GUI_VinylApply(
            gui_music_transport_is_playing(&gui_music_transport),
            vinyl_reset);
    }

    /*
     * Playback 进度（未落地）
     * ------------------------------------------------------------
     * playing 且有当前曲时，向后台问 position_ms / duration_ms，换成
     * 0..100 再 set_progress 并 ProgressApply。不要用 GUI 墙钟累加，
     * 也不要把走表零头放进 transport 结构体。
     * ------------------------------------------------------------
     */

    if (progress_changed)
    {
        (void)Service_GUI_ProgressApply(
            gui_music_transport_get_progress(&gui_music_transport));
    }
}
