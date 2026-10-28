/**
  ******************************************************************************
  * @file    gui_music.c
  * @brief   音乐分区：把本分区输入翻译成播放命令、推进窗口协议、按播放快照刷新显示。
  *
  * @details
  *          只经 #71 公开合同的 playback_command_send() 与 playback_snapshot_read()
  *          与播放任务交互（#72 A1）；不读写播放游标、不持 playing 真值（A2）。
  *          每圈先处理输入，再读一次快照交给 gui_music_transport 归约，与上次显示
  *          的结果比较后只对变化的部分调用 Service_GUI_*（#72 C 节）。曲名按归约
  *          已追上的曲目身份经名称槽向 Storage 请求（gui_music_title）。不打开文件、
  *          不解码，不使用 LVGL。listbuffer 与 namebuffer 头只出现在本分区。
  ******************************************************************************
  */

#include "gui_music.h"
#include "gui_music_queue_rows.h"
#include "gui_music_queue_window.h"
#include "gui_music_title.h"
#include "gui_music_transport.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "APP/tasks/playback/playback_contract.h"
#include "APP/tasks/storage/catalog/storage_listbuffer.h"
#include "APP/tasks/storage/catalog/storage_namebuffer.h"
#include "Service/log/log_service.h"

/** @brief 本分区日志 tag。 */
static const char gui_music_log_tag[] = "gui_music";

static GUI_MusicQueueWindowClientTypeDef gui_music_queue_window_client;

/** @brief 曲名客户：身份采信与名称槽握手。 */
static GUI_MusicTitleClientTypeDef gui_music_title_client;

/** @brief 快照归约状态：命令序号、最后一份已追上的快照与各规则的历史基准。 */
static GUI_MusicTransportTypeDef gui_music_transport;

/** @brief 已写到界面的内容：显示决定与交给 Service 的 Queue 高亮。 */
typedef struct
{
    /** 上一圈已写到界面的显示决定；has_decision 为假时首圈全部写入。 */
    GUI_MusicTransportDecisionTypeDef decision;
    bool has_decision;
    /** 最近一次经 QueueApply 交给 Service 的高亮下标。 */
    uint16_t highlight;
    /**
     * Service 的 Queue 高亮可能已不等于 highlight。点行时 Service 先即时高亮
     * 本行（GUI-17），本分区不知道是哪一行；回显追上后须无条件重套一次，
     * 即使快照所示高亮与上次相同。
     */
    bool highlight_stale;
} GUI_MusicShownTypeDef;

/** @brief 已写到界面的内容。 */
static GUI_MusicShownTypeDef gui_music_shown;

/**
 * @brief 判断已显示窗口能否立即重套：仍显示、槽位 IDLE 且卷仍挂载。
 * @return 为真时可以读 storage_listbuffer 的 Rows 重新 QueueApply。
 * @note  槽位不是 IDLE 时 Rows 可能正被 Storage 改写，不能读。
 */
static bool gui_music_displayed_window_idle(void)
{
    return gui_music_queue_window_client.displayed &&
           (storage_listbuffer.Status == STORAGE_LISTBUFFER_IDLE) &&
           storage_task_sd_is_mounted();
}

/**
 * @brief 把窗口槽 Rows 里的曲目显示名与歌手按行填进 Queue，并记下交给 Service 的高亮。
 * @param[in] highlight_index 应高亮的播放列表位置；无高亮为 GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT。
 * @return QueueApply 的返回值。
 * @note  卷已卸载时不读 Rows，填空窗以免画出已拔卡曲目。
 */
static Service_StatusTypeDef gui_music_apply_window(uint16_t highlight_index)
{
    const char *titles[STORAGE_LISTBUFFER_MAX_ENTRIES];
    const char *artists[STORAGE_LISTBUFFER_MAX_ENTRIES];
    uint16_t length;

    gui_music_shown.highlight = highlight_index;
    gui_music_shown.highlight_stale = false;

    if (!storage_task_sd_is_mounted())
    {
        return Service_GUI_QueueApply(NULL, NULL, 0U, 0U, SERVICE_GUI_QUEUE_NO_CURRENT);
    }

    length = gui_music_queue_rows(&storage_listbuffer, titles, artists);
    if (length == 0U)
    {
        return Service_GUI_QueueApply(NULL, NULL, 0U, 0U, highlight_index);
    }

    return Service_GUI_QueueApply(
        titles,
        artists,
        length,
        storage_listbuffer.Index,
        highlight_index);
}

/**
 * @brief 给命令编号并发送；队列满时丢弃本次输入并记日志（B5、B6）。
 * @param[in] kind 命令种类。
 * @param[in] generation 仅 SELECT：已显示窗口的曲库代次；其他命令传 0。
 * @param[in] index 仅 SELECT：播放列表位置；其他命令传 0。
 * @return 为真表示命令已入队。
 * @note  只由 gui_music_send_plain 与 gui_music_send_select 调用。
 */
static bool gui_music_send_command(
    Playback_CommandKindTypeDef kind,
    uint32_t generation,
    uint16_t index)
{
    if (gui_music_transport_send(&gui_music_transport, kind, generation, index) == PLAYBACK_OK)
    {
        return true;
    }

    (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, gui_music_log_tag,
                           "Playback command queue full, input dropped.");
    return false;
}

/**
 * @brief 发送不带参数的命令（PREV、NEXT、TOGGLE）。
 * @param[in] kind 命令种类。
 * @return 为真表示命令已入队。
 */
static bool gui_music_send_plain(Playback_CommandKindTypeDef kind)
{
    return gui_music_send_command(kind, 0U, 0U);
}

/**
 * @brief 发送 SELECT 命令。
 * @param[in] generation 已显示窗口的曲库代次。
 * @param[in] index 播放列表位置。
 * @return 为真表示命令已入队。
 */
static bool gui_music_send_select(uint32_t generation, uint16_t index)
{
    return gui_music_send_command(PLAYBACK_COMMAND_SELECT, generation, index);
}

/**
 * @brief 处理本分区输入：三键与点行发命令，进度条松手只要求回位（B1–B4）。
 * @param[in] input 本圈命令；NULL 视为 NONE。
 * @return 为真表示进度条刚松手，本圈须按快照重写一次进度条（B3）。
 */
static bool gui_music_dispatch_input_seek_released(const Service_GUI_InputTypeDef *input)
{
    Service_GUI_InputCommandTypeDef command;
    uint32_t window_generation;

    command = (input == NULL) ? SERVICE_GUI_INPUT_NONE : input->command;

    switch (command)
    {
        case SERVICE_GUI_INPUT_MUSIC_QUEUE_SELECT:
            window_generation = gui_music_queue_window_displayed_generation(
                &gui_music_queue_window_client);
            if (gui_music_send_select(window_generation, input->param))
            {
                /* Service 已即时高亮本行；回显追上后无条件按快照重套。 */
                gui_music_shown.highlight_stale = true;
            }
            else if (gui_music_displayed_window_idle())
            {
                /* 队列满：立即按最后一份已追上的快照撤回即时高亮，不受“未追上不覆盖”限制（B6）。 */
                (void)gui_music_apply_window(gui_music_transport_withdraw_highlight(
                    &gui_music_transport,
                    window_generation));
            }
            else
            {
                /* 窗口正在换，Rows 不可读；等下一次 APPLY 或回显追上时按快照重套。 */
                gui_music_shown.highlight_stale = true;
            }
            break;

        case SERVICE_GUI_INPUT_MUSIC_PREVIOUS:
            (void)gui_music_send_plain(PLAYBACK_COMMAND_PREV);
            break;

        case SERVICE_GUI_INPUT_MUSIC_NEXT:
            (void)gui_music_send_plain(PLAYBACK_COMMAND_NEXT);
            break;

        case SERVICE_GUI_INPUT_MUSIC_PLAY_PAUSE:
            (void)gui_music_send_plain(PLAYBACK_COMMAND_TOGGLE);
            break;

        case SERVICE_GUI_INPUT_MUSIC_SEEK:
            /* 不支持真定位：松手不发命令，只按当前快照回位。 */
            return true;

        case SERVICE_GUI_INPUT_NONE:
        default:
            break;
    }

    return false;
}

/**
 * @brief 推进窗口协议一步：REQUEST / APPLY / CLEAR。
 * @return 本圈的窗口动作。
 * @note  APPLY 的高亮按最后一份已追上的快照与新窗口代次计算（C3 后三条），与
 *        B6 撤回同一规则；CLEAR 只清空 Queue，三态、唱盘与时间由快照驱动（C10）。
 */
static GUI_MusicQueueWindowActionTypeDef gui_music_window_step(void)
{
    GUI_MusicQueueWindowActionTypeDef action;

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
                gui_music_queue_window_request_index(&gui_music_queue_window_client),
                STORAGE_LISTBUFFER_MAX_ENTRIES,
                0U);
            break;

        case GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY:
            if ((gui_music_apply_window(gui_music_transport_withdraw_highlight(
                     &gui_music_transport,
                     storage_listbuffer.Generation)) == SERVICE_OK) &&
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
                    applied_length,
                    storage_listbuffer.Generation);
            }

            storage_listbuffer.Status = STORAGE_LISTBUFFER_IDLE;
            break;

        case GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR:
            (void)Service_GUI_QueueApply(NULL, NULL, 0U, 0U, SERVICE_GUI_QUEUE_NO_CURRENT);
            gui_music_shown.highlight = GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT;
            gui_music_shown.highlight_stale = false;
            break;

        default:
            break;
    }

    return action;
}

/**
 * @brief 推进曲名一步：按已追上的曲目身份推进名称槽握手，只在显示变化时调 TitleApply。
 * @param[in] decision 本圈归约结果，提供采信的曲目身份。
 * @note  先读槽状态，READY 时再读应答代次与两列：READY 期间 Storage 不再写槽。
 *        曲名与歌手由曲名客户一起给出、同一次 TitleApply 写入（GUI-26），Service 拷完
 *        文本后才把槽写回 IDLE；空白与 `--` 只在与上一次不同时写。
 */
static void gui_music_title_step(const GUI_MusicTransportDecisionTypeDef *decision)
{
    GUI_MusicTitleIdentityTypeDef identity;
    GUI_MusicTitleIdentityTypeDef requested;
    GUI_MusicTitleActionTypeDef action;
    GUI_MusicTitleTextTypeDef text;
    uint32_t status;
    uint32_t reply_generation;

    identity.has_track = decision->has_track;
    identity.generation = decision->generation;
    identity.index = decision->index;

    status = storage_namebuffer.Status;
    reply_generation = (status == STORAGE_NAMEBUFFER_READY) ? storage_namebuffer.Generation : 0U;
    action = gui_music_title_poll(&gui_music_title_client, &identity, status, reply_generation);

    if (action == GUI_MUSIC_TITLE_ACTION_REQUEST)
    {
        requested = gui_music_title_request_identity(&gui_music_title_client);
        if (storage_namebuffer_request(requested.index, requested.generation) != STORAGE_OK)
        {
            gui_music_title_request_failed(&gui_music_title_client);
        }
    }

    /* 只有 ACCEPT 读槽内两列；READY 期间 Storage 不写槽，Service 拷完文本后才写回 IDLE。 */
    if (gui_music_title_text(&gui_music_title_client,
                             action,
                             storage_namebuffer.Title,
                             storage_namebuffer.Artist,
                             &text))
    {
        (void)Service_GUI_TitleApply(text.title, text.artist);
    }

    if ((action == GUI_MUSIC_TITLE_ACTION_ACCEPT) || (action == GUI_MUSIC_TITLE_ACTION_DISCARD))
    {
        storage_namebuffer.Status = STORAGE_NAMEBUFFER_IDLE;
    }
}

/**
 * @brief 把一次显示决定写到界面：只对与上次不同的部分调用 Service_GUI_*。
 * @param[in] decision 本圈归约结果。
 * @param[in] seek_released 进度条刚松手，须无条件重写进度条（B3）。
 * @note  一次性事件（唱盘归零、跳过增量、闪烁）只在产生它的这一圈出现，必须在
 *        本圈消费，不能与上次比较后丢弃。状态行的优先级与计时由 Service 持有。
 */
static void gui_music_show(const GUI_MusicTransportDecisionTypeDef *decision, bool seek_released)
{
    const bool first = !gui_music_shown.has_decision;

    if (first || (decision->state != gui_music_shown.decision.state))
    {
        (void)Service_GUI_TransportApply(decision->state == PLAYBACK_STATE_PLAYING);
    }

    if (first || seek_released ||
        (decision->progress_percent != gui_music_shown.decision.progress_percent))
    {
        (void)Service_GUI_ProgressApply(decision->progress_percent);
    }

    if (first ||
        (decision->position_ms != gui_music_shown.decision.position_ms) ||
        (decision->duration_ms != gui_music_shown.decision.duration_ms))
    {
        (void)Service_GUI_TimeApply(decision->position_ms, decision->duration_ms);
    }

    /* 唱盘（C8）：三态变化时起转/停转；归零是一次性事件，出现即调用。 */
    if (first || decision->vinyl_reset_angle ||
        (decision->vinyl_playing != gui_music_shown.decision.vinyl_playing))
    {
        (void)Service_GUI_VinylApply(decision->vinyl_playing, decision->vinyl_reset_angle);
    }

    /*
     * 状态行（C6、C7）：常驻短语先于跳过提示，同一圈既进入 ALL_BAD 又新增跳过时，
     * 提示被常驻短语挡掉（Service 内“常驻 > 提示 > 时间”）。短语指向模块内静态表，
     * 按指针比较即可。跳过增量与闪烁是一次性事件，出现即调用。
     */
    if (first || (decision->status_phrase != gui_music_shown.decision.status_phrase))
    {
        (void)Service_GUI_StatusApply(decision->status_phrase);
    }

    if (decision->skip_added != 0U)
    {
        (void)Service_GUI_SkipNoticeApply(decision->skip_added);
    }

    if (decision->status_flash)
    {
        (void)Service_GUI_StatusFlash();
    }

    gui_music_shown.decision = *decision;
    gui_music_shown.has_decision = true;
}

/**
 * @brief 复位窗口客户、曲名客户与快照归约，并把播放键画成 PLAY、进度条为 0、唱盘归零。
 * @note  时间 Label 的初始 `0:00/--:--` 与曲名的初始 `--` 由 Service 在 Init 时写入。首圈 step 按
 *        快照把各部分全部写一次。
 */
void gui_music_init(void)
{
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_IDLE == STORAGE_LISTBUFFER_IDLE,
                   "music queue window IDLE must match listbuffer");
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_PENDING == STORAGE_LISTBUFFER_PENDING,
                   "music queue window PENDING must match listbuffer");
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_READY == STORAGE_LISTBUFFER_READY,
                   "music queue window READY must match listbuffer");
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS < STORAGE_LISTBUFFER_MAX_ENTRIES,
                   "music queue back rows must leave room for visible rows in one window");
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT == SERVICE_GUI_QUEUE_NO_CURRENT,
                   "music queue window no-current must match QueueApply sentinel");
    _Static_assert(GUI_MUSIC_TITLE_IDLE == STORAGE_NAMEBUFFER_IDLE,
                   "music title IDLE must match namebuffer");
    _Static_assert(GUI_MUSIC_TITLE_PENDING == STORAGE_NAMEBUFFER_PENDING,
                   "music title PENDING must match namebuffer");
    _Static_assert(GUI_MUSIC_TITLE_READY == STORAGE_NAMEBUFFER_READY,
                   "music title READY must match namebuffer");

    gui_music_queue_window_client_init(&gui_music_queue_window_client);
    gui_music_transport_init(&gui_music_transport);
    gui_music_title_client_init(&gui_music_title_client);
    gui_music_shown.has_decision = false;
    gui_music_shown.highlight = GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT;
    gui_music_shown.highlight_stale = false;
    (void)Service_GUI_TransportApply(false);
    (void)Service_GUI_ProgressApply(0U);
    (void)Service_GUI_VinylApply(false, true);
}

/**
 * @brief 推进音乐分区一步：输入发命令、读快照归约、窗口协议、曲名、按需刷新显示。
 * @param[in] input 本圈已 Consume 的输入；NULL 视为 NONE。
 * @note  只由 GUI Task 调用。每圈只读一次快照；高亮在窗口仍显示且槽位 IDLE 时
 *        才重套（C3），Queue 不随当前曲自动滚动。
 */
void gui_music_step(const Service_GUI_InputTypeDef *input)
{
    Playback_SnapshotTypeDef snapshot;
    GUI_MusicTransportDecisionTypeDef decision;
    GUI_MusicQueueWindowActionTypeDef action;
    bool seek_released;

    seek_released = gui_music_dispatch_input_seek_released(input);

    playback_snapshot_read(&snapshot);
    gui_music_transport_reduce(
        &gui_music_transport,
        &snapshot,
        gui_music_queue_window_displayed_generation(&gui_music_queue_window_client),
        &decision);

    action = gui_music_window_step();
    gui_music_title_step(&decision);

    /* APPLY 已按快照重套本窗；其余情况在高亮变化或 Service 即时高亮待覆盖时重套。 */
    if (decision.highlight_apply &&
        (action != GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY) &&
        (gui_music_shown.highlight_stale ||
         (decision.highlight_index != gui_music_shown.highlight)) &&
        gui_music_displayed_window_idle())
    {
        (void)gui_music_apply_window(decision.highlight_index);
    }

    gui_music_show(&decision, seek_released);
}
