/**
  ******************************************************************************
  * @file    gui_music.c
  * @brief   音乐分区：把本分区输入翻译成播放命令、写最新需求并读前台任务的结果、按播放快照刷新显示。
  *
  * @details
  *          只经 #71 公开合同的 playback_command_send() 与 playback_snapshot_read()
  *          与播放任务交互（#72 A1）；不读写播放游标、不持 playing 真值（A2）。
  *          每圈先处理输入，再读一次快照交给 gui_music_transport 归约，与上次显示
  *          的结果比较后只对变化的部分调用 Service_GUI_*（#72 C 节）。曲名按归约
  *          已追上的曲目身份写曲名需求（gui_music_title）；Queue 窗口按滚动算出的起点写窗口需求
  *          （gui_music_queue_window）。需求和结果都经 ui_cell 的格子交接（`music_cells.h`）：GUI 不等文件、不查询卷状态，
  *          只写最新需求、读结果；结果在锁外套用，GUI 自己留一份窗口副本。不打开文件、不解码，不使用 LVGL。
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
#include <string.h>

#include "APP/apps/music/music_cells.h"
#include "APP/apps/music/music_display_stats.h"
#include "APP/apps/music/music_hook_pageturn.h"
#include "APP/tasks/playback/playback_contract.h"
#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 本分区日志 tag。 */
static const char gui_music_log_tag[] = "gui_music";

static GUI_MusicQueueWindowClientTypeDef gui_music_queue_window_client;

/** @brief 曲名客户：身份采信与曲名需求、结果的序号握手。 */
static GUI_MusicTitleClientTypeDef gui_music_title_client;

/*
 * GUI 一侧的两份结果副本（窗口约 6 KiB、曲名约 0.5 KiB），放 AXI SRAM 新段 `.app_axi_ram`（NOLOAD，不自动清零，
 * `gui_music_init()` 整块写入）。GUI 在锁外套用副本，ui_cell 持锁只拷内存；重套高亮、套用失败重试都直接用副本，
 * 不再受“槽为 IDLE”限制。
 */
static MusicCells_WindowTypeDef gui_music_window_copy __attribute__((section(MUSIC_AXI_SECTION), aligned(4)));
static MusicCells_TitleResultTypeDef gui_music_title_copy __attribute__((section(MUSIC_AXI_SECTION), aligned(4)));

/** @brief 读者持有的已见序号：先不拿锁比较序号，变了才拿锁拷贝。 */
static uint32_t gui_music_window_seen;
static uint32_t gui_music_title_seen;

/** @brief 显示延迟汇总用（Debug）：最近一次写出的窗口需求、曲名需求的 tick（毫秒）和窗口起点；只记一个时刻，不拿锁、不投日志。 */
static uint32_t gui_music_window_request_tick;
static uint16_t gui_music_window_request_index;
static bool gui_music_window_request_open;
static uint32_t gui_music_title_request_tick;

/** @brief 翻页注入钩子（默认关闭）：上一次看到的 `TrackStartCount`，以及是否已经有基准。 */
static uint32_t gui_music_hook_track_start_count;
static bool gui_music_hook_has_baseline;

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
 * @brief 判断已显示窗口能否立即重套：Queue 仍显示着一窗。
 * @return 为真时可以读 GUI 的窗口副本重新 QueueApply（副本是 GUI 自己的，不受前台任务写入的影响）。
 */
static bool gui_music_displayed_window_idle(void)
{
    return gui_music_queue_window_client.displayed;
}

/**
 * @brief 把窗口副本里的曲目显示名与歌手按行填进 Queue，并记下交给 Service 的高亮。
 * @param[in] highlight_index 应高亮的播放列表位置；无高亮为 GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT。
 * @return QueueApply 的返回值。
 */
static Service_StatusTypeDef gui_music_apply_window(uint16_t highlight_index)
{
    const char *titles[MUSIC_WINDOW_MAX_ENTRIES];
    const char *artists[MUSIC_WINDOW_MAX_ENTRIES];
    uint16_t length;

    gui_music_shown.highlight = highlight_index;
    gui_music_shown.highlight_stale = false;

    length = gui_music_queue_rows(&gui_music_window_copy, titles, artists);
    if (length == 0U)
    {
        return Service_GUI_QueueApply(NULL, NULL, 0U, 0U, highlight_index);
    }

    return Service_GUI_QueueApply(
        titles,
        artists,
        length,
        gui_music_window_copy.Index,
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
 * @brief 处理本分区输入：三键与点行发命令，进度条手势交给跳转归约（GUI-10）。
 * @param[in] input 本圈命令；NULL 视为 NONE。
 * @return 为真表示进度条刚松手或触摸丢失：Service 的进度条停在手指处，本圈须按
 *         决定无条件重写一次（在途跳转显示目标，否则回到快照）。
 */
static bool gui_music_dispatch_input_seek_settled(const Service_GUI_InputTypeDef *input)
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

        case SERVICE_GUI_INPUT_MUSIC_SEEK_PRESS:
            gui_music_transport_seek_press(&gui_music_transport, input->param);
            break;

        case SERVICE_GUI_INPUT_MUSIC_SEEK_DRAG:
            gui_music_transport_seek_drag(&gui_music_transport, input->param);
            break;

        case SERVICE_GUI_INPUT_MUSIC_SEEK_RELEASE:
            if (gui_music_transport_seek_release(&gui_music_transport, input->param) ==
                GUI_MUSIC_TRANSPORT_SEEK_QUEUE_FULL)
            {
                /* 队列满：丢弃本次输入、记日志，进度条回到快照（GUI-17）。 */
                (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, gui_music_log_tag,
                                       "Playback command queue full, input dropped.");
            }
            return true;

        case SERVICE_GUI_INPUT_MUSIC_SEEK_CANCEL:
            gui_music_transport_seek_cancel(&gui_music_transport);
            return true;

        case SERVICE_GUI_INPUT_NONE:
        default:
            break;
    }

    return false;
}

/**
 * @brief 推进窗口一帧：读窗口结果（变了才读）、套用或清空，再按滚动算出的起点写最新窗口需求。
 * @return 本帧对窗口结果的动作。
 * @note  APPLY 的高亮按最后一份已追上的快照与新窗口代次计算（C3 后三条），与 B6 撤回同一规则；CLEAR 只清空 Queue，
 *        三态、唱盘与时间由快照驱动（C10）。窗口不比需求序号：晚到的旧窗口也照常套用（#200）。拿不到锁时读写都下一帧再试。
 *        套用失败从自己的窗口副本重试，不用重新要。
 */
static GUI_MusicQueueWindowActionTypeDef gui_music_window_step(void)
{
    GUI_MusicQueueWindowActionTypeDef action;
    bool new_result;

    gui_music_queue_window_note_lead(
        &gui_music_queue_window_client,
        Service_GUI_QueueScrollLead(),
        MUSIC_WINDOW_MAX_ENTRIES);

    new_result = ui_cell_read_if_changed(&music_cell_window_result, &gui_music_window_copy, &gui_music_window_seen) &&
                 gui_music_window_copy.Published;

    action = gui_music_queue_window_poll(
        &gui_music_queue_window_client,
        new_result,
        gui_music_window_copy.Generation);

    switch (action)
    {
        case GUI_MUSIC_QUEUE_WINDOW_ACTION_NONE:
            break;

        case GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY:
            if (gui_music_apply_window(gui_music_transport_withdraw_highlight(
                    &gui_music_transport,
                    gui_music_window_copy.Generation)) == SERVICE_OK)
            {
                uint16_t applied_index;
                uint16_t applied_length;

                applied_length = gui_music_window_copy.Length;
                applied_index = gui_music_window_copy.Index;
                if (applied_length == 0U)
                {
                    applied_index = 0U;
                }
                else if (applied_length > MUSIC_WINDOW_MAX_ENTRIES)
                {
                    applied_length = MUSIC_WINDOW_MAX_ENTRIES;
                }

                gui_music_queue_window_mark_applied(
                    &gui_music_queue_window_client,
                    applied_index,
                    applied_length,
                    gui_music_window_copy.Generation);

                /* 显示延迟：套用的窗口起点正是最近一次写出的需求时，量写需求到套用的时间。 */
                if (gui_music_window_request_open && (gui_music_window_copy.Index == gui_music_window_request_index))
                {
                    gui_music_window_request_open = false;
                    music_display_stats_note_window((uint32_t)((uint32_t)xTaskGetTickCount() - gui_music_window_request_tick));
                }
            }
            else
            {
                gui_music_queue_window_apply_failed(&gui_music_queue_window_client);
            }
            break;

        case GUI_MUSIC_QUEUE_WINDOW_ACTION_CLEAR:
            (void)Service_GUI_QueueApply(NULL, NULL, 0U, 0U, SERVICE_GUI_QUEUE_NO_CURRENT);
            gui_music_shown.highlight = GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT;
            gui_music_shown.highlight_stale = false;
            music_display_stats_note_clear((uint32_t)xTaskGetTickCount());
            break;

        default:
            break;
    }

    if (gui_music_queue_window_request_due(&gui_music_queue_window_client))
    {
        MusicCells_WindowRequestTypeDef request;

        request.Index = gui_music_queue_window_request_index(&gui_music_queue_window_client);
        request.Valid = true;
        if (ui_cell_try_write(&music_cell_window_request, &request))
        {
            gui_music_queue_window_request_sent(&gui_music_queue_window_client);
            gui_music_window_request_index = request.Index;
            gui_music_window_request_tick = (uint32_t)xTaskGetTickCount();
            gui_music_window_request_open = true;
        }
    }

    return action;
}

/**
 * @brief 推进曲名一帧：按已追上的曲目身份决定要不要写曲名需求，读结果（变了才读）按需求序号采信，只在显示变化时调 TitleApply。
 * @param[in] decision 本圈归约结果，提供采信的曲目身份。
 * @note  需求写不进去（拿不到锁）时下一帧再试。曲名与歌手由曲名客户一起给出、同一次 TitleApply 写入（GUI-26），
 *        空白与 `--` 只在与上一次不同时写。指向结果副本的文本在调 Apply 的这一帧内有效，Service 会拷走。
 */
static void gui_music_title_step(const GUI_MusicTransportDecisionTypeDef *decision)
{
    GUI_MusicTitleIdentityTypeDef identity;
    GUI_MusicTitleActionTypeDef action;
    GUI_MusicTitleTextTypeDef text;
    bool new_reply;

    identity.has_track = decision->has_track;
    identity.generation = decision->generation;
    identity.index = decision->index;

    new_reply = ui_cell_read_if_changed(&music_cell_title_result, &gui_music_title_copy, &gui_music_title_seen);
    action = gui_music_title_poll(&gui_music_title_client, &identity, new_reply ? &gui_music_title_copy : NULL);

    if (gui_music_title_request_due(&gui_music_title_client))
    {
        const MusicCells_TitleRequestTypeDef request = gui_music_title_request(&gui_music_title_client);

        if (ui_cell_try_write(&music_cell_title_request, &request))
        {
            gui_music_title_request_sent(&gui_music_title_client);
            gui_music_title_request_tick = (uint32_t)xTaskGetTickCount();
        }
    }

    if (gui_music_title_text(&gui_music_title_client,
                             action,
                             gui_music_title_copy.Title,
                             gui_music_title_copy.Artist,
                             &text))
    {
        (void)Service_GUI_TitleApply(text.title, text.artist);
    }

    if (action == GUI_MUSIC_TITLE_ACTION_ACCEPT)
    {
        music_display_stats_note_title((uint32_t)((uint32_t)xTaskGetTickCount() - gui_music_title_request_tick));
    }
}

/**
 * @brief 把一次显示决定写到界面：只对与上次不同的部分调用 Service_GUI_*。
 * @param[in] decision 本圈归约结果。
 * @param[in] seek_settled 进度条刚松手或触摸丢失，须无条件重写进度条。
 * @note  一次性事件（唱盘归零、取消拖动、跳过增量、闪烁）只在产生它的这一圈出现，必须在
 *        本圈消费，不能与上次比较后丢弃。状态行的优先级与计时由 Service 持有。
 */
static void gui_music_show(const GUI_MusicTransportDecisionTypeDef *decision, bool seek_settled)
{
    const bool first = !gui_music_shown.has_decision;

    if (first || (decision->state != gui_music_shown.decision.state))
    {
        (void)Service_GUI_TransportApply(decision->state == PLAYBACK_STATE_PLAYING);
    }

    /*
     * 进度条可否响应（GUI-10）：关闭时 Service 顺带结束在拖的按压。换曲取消拖动是
     * 一次性事件，须在 ProgressApply 之前结束 Service 的拖动，否则进度条仍被手指占着。
     */
    if (first || (decision->seek_enabled != gui_music_shown.decision.seek_enabled))
    {
        (void)Service_GUI_SeekEnable(decision->seek_enabled);
    }

    if (decision->seek_cancel)
    {
        (void)Service_GUI_SeekCancel();
    }

    if (first || seek_settled || decision->seek_cancel ||
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

    /* 唱盘（GUI-20）：三态变化时起转/停转；归零是一次性事件，出现即调用。 */
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
 *        快照把各部分全部写一次。GUI 副本在 NOLOAD 的 AXI 段，这里整块写入；已见序号归零，第一次读拿到格子的初值
 *        （窗口结果的 `Published` 为假，GUI 忽略）。GUI 侧留下 3 个静态断言（BACK_ROWS、NO_CURRENT、SEEK_SCALE）。
 */
void gui_music_init(void)
{
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS < MUSIC_WINDOW_MAX_ENTRIES,
                   "music queue back rows must leave room for visible rows in one window");
    _Static_assert(GUI_MUSIC_QUEUE_WINDOW_NO_CURRENT == SERVICE_GUI_QUEUE_NO_CURRENT,
                   "music queue window no-current must match QueueApply sentinel");
    _Static_assert(GUI_MUSIC_TRANSPORT_SEEK_SCALE == SERVICE_GUI_SEEK_SCALE,
                   "music transport seek scale must match Service_GUI seek event scale");

    (void)memset(&gui_music_window_copy, 0, sizeof(gui_music_window_copy));
    (void)memset(&gui_music_title_copy, 0, sizeof(gui_music_title_copy));
    gui_music_window_seen = 0U;
    gui_music_title_seen = 0U;
    gui_music_window_request_open = false;
    gui_music_hook_has_baseline = false;
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
 * @brief 翻页注入测试钩子（DIAG-7、R5.4，默认关闭）：每次看到新一首开始播放（`TrackStartCount` 变化），自动写一次翻页需求
 *        （窗口起点前进一窗，到列表末尾回到 0），并记下注入时刻供前台任务量块池补满的耗时。
 * @param[in] snapshot 本圈读到的播放快照。
 * @note  只写需求，不改窗口客户的记账：前台任务发布那一窗，GUI 照常套用（窗口不比序号）。第一次读到快照只记基准，不注入。
 *        钩子关闭时调用点写成 `if (MUSIC_HOOK_PAGE_TURN != 0U)`，默认构建也编译本函数。
 */
static void gui_music_page_turn_hook(const Playback_SnapshotTypeDef *snapshot)
{
    MusicCells_WindowRequestTypeDef request;
    const GUI_MusicQueueWindowClientTypeDef *client = &gui_music_queue_window_client;

    if (!gui_music_hook_has_baseline)
    {
        gui_music_hook_has_baseline = true;
        gui_music_hook_track_start_count = snapshot->TrackStartCount;
        return;
    }

    if (snapshot->TrackStartCount == gui_music_hook_track_start_count)
    {
        return;
    }
    gui_music_hook_track_start_count = snapshot->TrackStartCount;

    request.Valid = true;
    request.Index = (client->displayed && (client->applied_length >= MUSIC_WINDOW_MAX_ENTRIES))
                        ? (uint16_t)(client->applied_index + client->applied_length)
                        : 0U;
    if (ui_cell_try_write(&music_cell_window_request, &request))
    {
        music_hook_pageturn_note_injection((uint32_t)xTaskGetTickCount());
    }
}

/**
 * @brief 推进音乐分区一步：输入发命令、读快照归约、窗口协议、曲名、按需刷新显示。
 * @param[in] input 本圈已 Consume 的输入；NULL 视为 NONE。
 * @note  只由 GUI Task 调用。每圈只读一次快照；高亮在窗口仍显示时才重套（C3，用 GUI 自己的窗口副本，不再受槽状态限制），
 *        Queue 不随当前曲自动滚动。
 */
void gui_music_step(const Service_GUI_InputTypeDef *input)
{
    Playback_SnapshotTypeDef snapshot;
    GUI_MusicTransportDecisionTypeDef decision;
    GUI_MusicQueueWindowActionTypeDef action;
    bool seek_settled;

    seek_settled = gui_music_dispatch_input_seek_settled(input);

    playback_snapshot_read(&snapshot);
    gui_music_transport_reduce(
        &gui_music_transport,
        &snapshot,
        gui_music_queue_window_displayed_generation(&gui_music_queue_window_client),
        &decision);

    action = gui_music_window_step();
    gui_music_title_step(&decision);
    if (MUSIC_HOOK_PAGE_TURN != 0U)
    {
        gui_music_page_turn_hook(&snapshot);
    }

    /* APPLY 已按快照重套本窗；其余情况在高亮变化或 Service 即时高亮待覆盖时重套。 */
    if (decision.highlight_apply &&
        (action != GUI_MUSIC_QUEUE_WINDOW_ACTION_APPLY) &&
        (gui_music_shown.highlight_stale ||
         (decision.highlight_index != gui_music_shown.highlight)) &&
        gui_music_displayed_window_idle())
    {
        (void)gui_music_apply_window(decision.highlight_index);
    }

    gui_music_show(&decision, seek_settled);
}
