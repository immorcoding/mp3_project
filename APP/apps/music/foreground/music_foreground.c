/**
 * @file music_foreground.c
 * @brief 音乐前台流程：读曲名与 Queue 窗口各一行标签，经 `ui_cell` 发布界面数据（见 music_foreground.h）。
 *
 * @details
 *          状态都是静态的，只由前台任务访问，没有锁（锁只在 `ui_cell` 里，持锁只拷内存）：
 *          - 需求：两个格子里最新需求的副本，加“读到过没有”的序号；
 *          - 曲名：`title_pending`（有没处理的需求）和“上一次答的时候曲库是否可用”；
 *          - 窗口：正在拼的一窗（`fill`）和“窗口格里现在是什么”（`published`）。
 *          缓冲与状态全部放 `.apps_sdram`（ARC-28：应用内存默认放 SDRAM 应用段，DTCM 留给任务栈与内核）：行缓存、窗口组装缓冲、
 *          曲名组装区、曲名读取用的临时一行（504 B，不放前台任务的栈，spec G“step 里不放大局部变量”）、曲库一次读全的结果（约 300 B）
 *          和各状态变量；NOLOAD 不清零，`music_foreground_boot_init()` 整块写入。
 *
 *          一步的检查点只有一个：函数开头和结尾各看一次需求。一行之内（打开、取长度、至多几次读取、关闭）不中断，
 *          所以一步的上界是一次文件调用加约 1 ms CPU（ARC-26）：命中缓存的行不读文件，一步可以拼进好几行（每行一次 `memcpy`）。
 *
 *          卷不可用（拔卡的正常竞态，`NO_MEDIA`/`NOT_READY`/`INVALID_HANDLE`）不是文件错误：窗口行读到它时不填行、不发布，
 *          已发布的窗口保持不变，这一窗挂起等卷事件（卷消失由代次 0 的空窗路径收尾）或新需求（spec E“暂时不能读时窗口不变”）。
 *          `SERVICE_BUSY`（文件槽用尽）一律过约 10 ms 重试，不设次数上限，也不显示回退名（ARC-27）；日志只在第 1 次和每满
 *          `MUSIC_BUSY_LOG_EVERY` 次记一行，不刷屏。
 */

#include "APP/apps/music/foreground/music_foreground.h"

#include <stdio.h>
#include <string.h>

#include "APP/apps/music/catalog/catalog.h"
#include "APP/apps/music/catalog/catalog_track_diag.h"
#include "APP/apps/music/catalog/catalog_track_text.h"
#include "APP/apps/music/foreground/music_row_cache.h"
#include "APP/apps/music/music_cells.h"
#include "APP/apps/music/music_display_stats.h"
#include "APP/apps/music/music_hook_pageturn.h"
#include "APP/tasks/audio_reader/audio_reader_block.h"
#include "APP/tasks/foreground/foreground_task.h"
#include "APP/tasks/volume/volume_event.h"
#include "Service/log/log_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

static const char music_foreground_log_tag[] = "MUSIC";

/* 本文件所有静态状态与缓冲放 SDRAM 应用段（ARC-28），NOLOAD 不清零，由 `music_foreground_boot_init()` 整块写入后才使用；
 * 只有前台任务访问，没有 DMA，不需要 Cache 维护。大缓冲按 32 B 对齐，小状态不用。 */
#define MUSIC_FOREGROUND_SDRAM __attribute__((section(MUSIC_SDRAM_SECTION)))

/* ---- 最新需求的副本 ---- */
static uint32_t music_title_request_seen MUSIC_FOREGROUND_SDRAM;                    /* 已读到的曲名需求序号（ui_cell 序号，读者持有）。 */
static MusicCells_TitleRequestTypeDef music_title_request MUSIC_FOREGROUND_SDRAM;   /* 最新曲名需求。 */
static uint32_t music_window_request_seen MUSIC_FOREGROUND_SDRAM;                   /* 已读到的窗口需求序号。 */
static MusicCells_WindowRequestTypeDef music_window_request MUSIC_FOREGROUND_SDRAM; /* 最新窗口需求。 */

/* ---- 曲名的进度 ---- */
static bool music_title_pending MUSIC_FOREGROUND_SDRAM;   /* 最新曲名需求还没答（或需要再答一次）。 */
static bool music_title_answered MUSIC_FOREGROUND_SDRAM;  /* 最新曲名需求已经答过。 */
static uint32_t music_title_answered_catalog_generation MUSIC_FOREGROUND_SDRAM; /* 答的时候曲库的代次；0 表示当时曲库不可用，曲库发布后要再答一次。 */

/* ---- SERVICE_BUSY ---- */
static uint32_t music_busy_streak MUSIC_FOREGROUND_SDRAM; /* 连续遇到几次 SERVICE_BUSY（曲名与窗口行共用）；读到非 BUSY 的结果清零，只用来节制日志。 */

/* ---- 窗口的进度 ---- */
typedef struct
{
    bool Active;          /**< 正在拼一窗。 */
    bool VolumeWait;      /**< 读某一行时卷不可用：这一窗挂起，不填行、不发布，等卷事件（代次变）或新需求。 */
    uint32_t Generation;  /**< 开始拼时的曲库代次；途中变化即作废。 */
    uint16_t Index;       /**< 本窗起点；需求的起点变了就作废。 */
    uint16_t Filled;      /**< 已拼好的行数，也是下一行的行号。 */
} MusicForeground_FillTypeDef;

typedef struct
{
    bool Valid;           /**< 发布过。 */
    uint32_t Generation;  /**< 窗口格里现在的代次；0 表示空窗。 */
    uint16_t Index;       /**< 窗口格里现在的起点。 */
} MusicForeground_PublishedTypeDef;

static MusicForeground_FillTypeDef music_fill MUSIC_FOREGROUND_SDRAM;
static MusicForeground_PublishedTypeDef music_published MUSIC_FOREGROUND_SDRAM;

/* 组装缓冲（约 6 KiB）：整窗凑齐才一次性发布；每次开始拼窗时行数清零后才使用。 */
static MusicCells_WindowTypeDef music_assembly __attribute__((section(MUSIC_SDRAM_SECTION), aligned(32)));

/* 曲名结果的组装区（约 0.5 KiB）；发布前整份写入。 */
static MusicCells_TitleResultTypeDef music_title_assembly __attribute__((section(MUSIC_SDRAM_SECTION), aligned(32)));

/* 曲名读取用的临时一行（504 B）：命中缓存或读标签的结果先拷到这里再发布。不放前台任务的栈（1536 word 共用给所有应用）。 */
static MusicCells_RowTypeDef music_title_row __attribute__((section(MUSIC_SDRAM_SECTION), aligned(32)));

/* 曲库一次读全的结果（约 300 B）：不放前台任务的栈。 */
static Catalog_TrackTypeDef music_track MUSIC_FOREGROUND_SDRAM;

/* 本步是否已经用掉唯一一次文件调用（ARC-26）；每步开头清零。 */
static bool music_file_call_used MUSIC_FOREGROUND_SDRAM;

/* 前台任务第一次 step 时投一次钩子的启动行。 */
static bool music_announced MUSIC_FOREGROUND_SDRAM;

/**
 * @brief 清零行缓存键表和流程状态。
 * @note  Boot 任务调用（注册表的 boot_init 经 `music_boot_init()`），晚于可选的 SDRAM 自检、早于创建前台任务。
 *        本文件的状态与缓冲都在 NOLOAD 的 `.apps_sdram`，在这里整块写入；行缓存的行内容不清（靠键表判断有效，键表在这里清零）。
 */
void music_foreground_boot_init(void)
{
    music_row_cache_boot_init();
    music_title_request_seen = 0U;
    music_window_request_seen = 0U;
    (void)memset(&music_title_request, 0, sizeof(music_title_request));
    (void)memset(&music_window_request, 0, sizeof(music_window_request));
    music_title_pending = false;
    music_title_answered = false;
    music_title_answered_catalog_generation = 0U;
    music_busy_streak = 0U;
    (void)memset(&music_fill, 0, sizeof(music_fill));
    (void)memset(&music_published, 0, sizeof(music_published));
    music_file_call_used = false;
    music_announced = false;
    /* 组装缓冲、临时行和曲库读全结果都在 NOLOAD 的 .apps_sdram，不自动清零：第一次使用前整块写入（HWD-2）。 */
    (void)memset(&music_assembly, 0, sizeof(music_assembly));
    (void)memset(&music_title_assembly, 0, sizeof(music_title_assembly));
    (void)memset(&music_title_row, 0, sizeof(music_title_row));
    (void)memset(&music_track, 0, sizeof(music_track));
}

/**
 * @brief 定位符失效测试钩子（DIAG-7、R5.5，默认关闭）：把曲库读出的文件定位符清零（全零是“无定位符”），
 *        Filesystem 一定判核对不符，走真实的“退回按路径打开”路径。
 * @param[in,out] track 本次要读标签的曲库内容（路径与定位符属于同一曲库代次）。
 * @note  与音频读取任务共用同一个开关 `AUDIO_READER_HOOK_LOCATOR_MISS`（CMake 变量 `MP3_HOOK_LOCATOR_MISS`，只允许 Debug；
 *        经音频读取任务的公开头 `audio_reader_block.h` 取得，它再导出配置宏，STY-3），
 *        所以钩子打开时前台的曲名和窗口行也按路径打开：12000 首同文件夹时一次约 1.4 至 2.1 s、占 SD 卷锁，
 *        用来核对“前台回退按路径打开时切歌不显示 Read timeout”。写成 `if (开关)`，默认构建也编译。
 */
static void music_foreground_apply_locator_hook(Catalog_TrackTypeDef *track)
{
    if (AUDIO_READER_HOOK_LOCATOR_MISS != 0U)
    {
        (void)memset(&track->Locator, 0, sizeof(track->Locator));
    }
}

/**
 * @brief 判断这次读取的结果是不是“稳定”的：取到标签、无 ID3v2、帧缺失、解码失败、超读取上限。
 * @param[in] result 一次读取的结果，不得为空。
 * @return 稳定为真；打开失败（含 SERVICE_BUSY）和读取错误不稳定，不缓存（瞬时错误不能让这首歌在换卡前一直显示文件主名）。
 */
static bool music_foreground_result_is_stable(const CatalogTrackText_ResultTypeDef *result)
{
    return !result->OpenFailed && (result->Text.Result != CATALOG_TEXT_READ_ERROR);
}

/**
 * @brief 把一次读取的显示文本拷成一行。
 * @param[out] row 行。
 * @param[in] result 一次读取的结果，不得为空。
 */
static void music_foreground_row_from_result(MusicCells_RowTypeDef *row, const CatalogTrackText_ResultTypeDef *result)
{
    (void)memcpy(row->Title, result->Text.Title, sizeof(row->Title));
    (void)memcpy(row->Artist, result->Text.Artist, sizeof(row->Artist));
}

/**
 * @brief 把一行清成两列空串。
 * @param[out] row 行。
 */
static void music_foreground_row_blank(MusicCells_RowTypeDef *row)
{
    row->Title[0] = '\0';
    row->Artist[0] = '\0';
}

/**
 * @brief 读两个需求格，有新的就换掉副本。
 * @return 为真表示“序号变了但锁被占着没读到”：调用者不能睡下，要尽快重试（否则没有人再叫醒前台任务）。
 * @note  GUI 写需求时已叫醒前台任务；这里只负责读。曲名需求变了就标记待处理，窗口需求变了由 `window_needs_work()` 按副本判断。
 */
static bool music_foreground_read_requests(void)
{
    MusicCells_TitleRequestTypeDef title_request;
    MusicCells_WindowRequestTypeDef window_request;
    bool lock_busy = false;

    if (ui_cell_read_if_changed(&music_cell_title_request, &title_request, &music_title_request_seen))
    {
        music_title_request = title_request;
        music_title_pending = music_title_request.Wanted;
        music_title_answered = false;
    }
    else if (ui_cell_changed(&music_cell_title_request, music_title_request_seen))
    {
        lock_busy = true;
    }

    if (ui_cell_read_if_changed(&music_cell_window_request, &window_request, &music_window_request_seen))
    {
        music_window_request = window_request;
    }
    else if (ui_cell_changed(&music_cell_window_request, music_window_request_seen))
    {
        lock_busy = true;
    }

    return lock_busy;
}

/**
 * @brief 曲库发布后（或卷就绪后）主动重新处理曲名需求：上一次答的时候曲库不可用，现在可用了。
 * @note  GUI 不会重写没变的需求，所以前台要自己再答一次。答案带回同一个需求序号，GUI 按序号采信。
 */
static void music_foreground_revisit_title(void)
{
    if (music_title_request.Wanted && music_title_answered && !music_title_pending &&
        (music_title_answered_catalog_generation == 0U) && (catalog_generation() != 0U))
    {
        music_title_pending = true;
    }
}

/**
 * @brief 发布曲名结果（整份写入结果格，写后不叫醒 GUI）。
 * @param[in] sequence 所答的需求序号。
 * @param[in] generation 实际代次；0 表示取不到。
 * @param[in] title 曲目显示名；NULL 当空串。
 * @param[in] artist 歌手；NULL 当空串。
 * @param[in] catalog_generation_seen 答的时候曲库的代次（0 表示当时曲库不可用）。
 */
static void music_foreground_publish_title(uint32_t sequence,
                                           uint32_t generation,
                                           const char *title,
                                           const char *artist,
                                           uint32_t catalog_generation_seen)
{
    music_title_assembly.RequestSequence = sequence;
    music_title_assembly.Generation = generation;
    music_title_assembly.Title[0] = '\0';
    music_title_assembly.Artist[0] = '\0';
    if (generation != 0U)
    {
        (void)strncpy(music_title_assembly.Title, (title != NULL) ? title : "", sizeof(music_title_assembly.Title) - 1U);
        (void)strncpy(music_title_assembly.Artist, (artist != NULL) ? artist : "", sizeof(music_title_assembly.Artist) - 1U);
        music_title_assembly.Title[sizeof(music_title_assembly.Title) - 1U] = '\0';
        music_title_assembly.Artist[sizeof(music_title_assembly.Artist) - 1U] = '\0';
    }

    ui_cell_write(&music_cell_title_result, &music_title_assembly);
    music_title_pending = false;
    music_title_answered = true;
    music_title_answered_catalog_generation = catalog_generation_seen;
}

/**
 * @brief 投一条曲名取不到的防御 WARN：`Name fail idx=<位置> gen=<请求代次>`，只含位置与代次，不含名字。
 * @param[in] index 播放列表位置。
 * @param[in] requested_generation 请求代次。
 */
static void music_foreground_warn_name_fail(uint16_t index, uint32_t requested_generation)
{
    char text[48];

    (void)snprintf(text, sizeof(text), "Name fail idx=%u gen=%lu", (unsigned)index, (unsigned long)requested_generation);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, music_foreground_log_tag, text);
}

/**
 * @brief 记一次 SERVICE_BUSY（文件槽用尽，瞬时的）：连续次数加一，第 1 次和之后每满 `MUSIC_BUSY_LOG_EVERY` 次记一行 WARN。
 * @param[in] position 播放列表位置（只含位置，不含文件名）。
 * @note  日志格式 `SERVICE_BUSY pos=<位置> n=<第几次>`，标签 `MUSIC`；节拍写进消息池核算（最坏与音频读取任务的同类行同时到达）。
 *        不设重试上限：BUSY 不是文件错误，卷事件、代次变化或新需求才会终止这次读取（ARC-27）。
 */
static void music_foreground_note_busy(uint16_t position)
{
    char text[40];

    music_busy_streak++;
    if ((music_busy_streak == 1U) || ((music_busy_streak % (uint32_t)MUSIC_BUSY_LOG_EVERY) == 0U))
    {
        (void)snprintf(text, sizeof(text), "SERVICE_BUSY pos=%u n=%lu", (unsigned)position, (unsigned long)music_busy_streak);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, music_foreground_log_tag, text);
    }
}

/**
 * @brief 处理曲名需求：命中行缓存不读文件，否则读一首的标签（本步唯一的文件调用），发布结果。
 * @return SERVICE_BUSY 的重试等待（毫秒）：大于 0 表示这次没答成，过这么久再来（一律重试）；0 表示已答（或不用答）。
 * @note  代次 0 表示“当前”。曲库不可用、代次不符、越界、卷没了都答代次 0 空名（GUI 显示 `--`），不缓存；
 *        只有“请求了确切代次却越界”和“文件主名也提取不出”是防御分支，记 WARN。
 */
static uint32_t music_foreground_process_title(void)
{
    const MusicCells_TitleRequestTypeDef request = music_title_request;
    MusicCells_RowTypeDef *const row = &music_title_row;
    const CatalogTrackText_ResultTypeDef *result;
    uint32_t start_cycles;
    bool found;

    found = catalog_read_track(request.Index, &music_track);

    if (music_track.Generation == 0U)
    {
        music_foreground_publish_title(request.Sequence, 0U, NULL, NULL, 0U);
        return 0U;
    }

    if ((request.Generation != 0U) && (request.Generation != music_track.Generation))
    {
        music_foreground_publish_title(request.Sequence, 0U, NULL, NULL, music_track.Generation);
        return 0U;
    }

    if (request.Index >= music_track.Count)
    {
        if (request.Generation != 0U)
        {
            music_foreground_warn_name_fail(request.Index, request.Generation);
        }
        music_foreground_publish_title(request.Sequence, 0U, NULL, NULL, music_track.Generation);
        return 0U;
    }

    if (!found)
    {
        /* 位置有效却取不到路径（路径太长等）：防御分支。 */
        music_foreground_warn_name_fail(request.Index, request.Generation);
        music_foreground_publish_title(request.Sequence, 0U, NULL, NULL, music_track.Generation);
        return 0U;
    }

    if (music_row_cache_get(music_track.Generation, request.Index, row))
    {
        music_foreground_publish_title(request.Sequence, music_track.Generation, row->Title, row->Artist, music_track.Generation);
        return 0U;
    }

    music_file_call_used = true;
    music_foreground_apply_locator_hook(&music_track);
    start_cycles = catalog_track_diag_begin();
    result = catalog_track_text_read(request.Index, &music_track);
    catalog_track_diag_name_done(request.Index, start_cycles, result);

    if (result == NULL)
    {
        if (!catalog_track_text_last_read_unavailable())
        {
            music_foreground_warn_name_fail(request.Index, request.Generation);
        }
        /* 读标签时卷不可用（拔卡的正常竞态）：不是文件错误，以空名收尾，不缓存。 */
        music_foreground_publish_title(request.Sequence, 0U, NULL, NULL, music_track.Generation);
        return 0U;
    }

    if (result->OpenBusy)
    {
        music_foreground_note_busy(request.Index);
        return MUSIC_BUSY_RETRY_MS; /* 文件槽用尽是瞬时的：不当文件错误，不显示回退名，过约 10 ms 重试，不设上限。 */
    }
    music_busy_streak = 0U;

    music_foreground_row_from_result(row, result);
    if (music_foreground_result_is_stable(result))
    {
        music_row_cache_put(music_track.Generation, request.Index, row);
    }
    music_foreground_publish_title(request.Sequence, music_track.Generation, row->Title, row->Artist, music_track.Generation);
    return 0U;
}

/**
 * @brief 发布窗口格里的内容：整份拷入窗口格，记下“现在是什么”，并结束正在拼的一窗。
 * @param[in] generation 窗口代次；0 表示空窗。
 * @param[in] index 窗口起点。
 * @param[in] length 窗口条数。
 */
static void music_foreground_publish_window(uint32_t generation, uint16_t index, uint16_t length)
{
    music_assembly.Published = true;
    music_assembly.Generation = generation;
    music_assembly.Index = index;
    music_assembly.Length = length;
    ui_cell_write(&music_cell_window_result, &music_assembly);

    music_published.Valid = true;
    music_published.Generation = generation;
    music_published.Index = index;
    music_fill.Active = false;
    music_fill.VolumeWait = false;
}

/**
 * @brief 发布一份代次 0 的空窗，并记下“因卷变化而发空窗”对应的卡检测边沿时刻（显示延迟汇总用）。
 * @note  代次变化、曲库不可用（扫描中、卷不在）时调用。只有窗口格里现在不是空窗时才会走到这里，所以不重复发布。
 */
static void music_foreground_publish_empty_window(void)
{
    uint32_t edge_tick;

    music_assembly.Length = 0U;
    if (volume_event_published_edge(VOLUME_ID_SD, &edge_tick))
    {
        music_display_stats_set_clear_origin(edge_tick);
    }
    music_foreground_publish_window(0U, 0U, 0U);
}

/**
 * @brief 窗口是否还有活：正在拼（挂起等卷事件的除外），或最新需求还没被窗口格里的内容满足。
 * @return 为真表示下一步还要处理窗口。
 * @note  “满足”= 窗口格里是当前代次、起点等于需求起点的窗口；曲库不可用时“满足”= 窗口格里已经是空窗。
 *        曲库发布后代次变成非 0，窗口格里的空窗不再满足，所以不需要 GUI 重写需求就会重新发窗。
 */
static bool music_foreground_window_needs_work(void)
{
    uint32_t generation;

    if (!music_window_request.Valid)
    {
        return false;
    }
    generation = catalog_generation();
    if (music_fill.Active)
    {
        /* 挂起等卷事件的一窗：代次和需求都没变就没有活（睡下等卷事件）；变了要马上处理（空窗收尾或从新起点重拼）。 */
        return !(music_fill.VolumeWait && (generation == music_fill.Generation) &&
                 (music_window_request.Index == music_fill.Index));
    }

    if (generation == 0U)
    {
        return !(music_published.Valid && (music_published.Generation == 0U));
    }
    return !(music_published.Valid && (music_published.Generation == generation) &&
             (music_published.Index == music_window_request.Index));
}

/**
 * @brief 开始拼一窗：清空组装缓冲的行数，记下起点与代次，记一次窗口开始（诊断）。
 * @param[in] generation 开始时的曲库代次。
 */
static void music_foreground_begin_fill(uint32_t generation)
{
    music_fill.Active = true;
    music_fill.VolumeWait = false;
    music_fill.Generation = generation;
    music_fill.Index = music_window_request.Index;
    music_fill.Filled = 0U;
    catalog_track_diag_window_begin();
}

/**
 * @brief 在本窗里再拼一行或几行：命中缓存的行不读文件（可以连拼），没命中的读一次文件（本步唯一的文件调用）；
 *        凑齐一窗（满 12 行或到列表末尾）就发布。
 * @return SERVICE_BUSY 的重试等待（毫秒）：大于 0 表示这一行没读成，过这么久再来（一律重试）；0 表示没有等待要求。
 * @note  每行先一次读全曲库（代次、条数、路径、定位符）：代次不是开始时的代次就作废、发空窗，等重新处理需求。
 *        读某一行时卷不可用：不填行、不发布，这一窗挂起（`VolumeWait`），已发布的窗口保持不变。
 */
static uint32_t music_foreground_fill_rows(void)
{
    for (;;)
    {
        const uint32_t position32 = (uint32_t)music_fill.Index + (uint32_t)music_fill.Filled;
        const uint16_t position = (uint16_t)((position32 > UINT16_MAX) ? UINT16_MAX : position32);
        MusicCells_RowTypeDef *row;
        const CatalogTrackText_ResultTypeDef *result;
        uint32_t start_cycles;
        bool found;

        if (music_fill.Filled >= (uint16_t)MUSIC_WINDOW_MAX_ENTRIES)
        {
            catalog_track_diag_window_end();
            music_foreground_publish_window(music_fill.Generation, music_fill.Index, music_fill.Filled);
            return 0U;
        }

        found = catalog_read_track(position, &music_track);
        if (music_track.Generation != music_fill.Generation)
        {
            /* 代次变了（重扫、拔卡、换卡）：不发半窗，发空窗收尾；随后按最新需求用新代次重新拼。 */
            catalog_track_diag_window_end();
            music_foreground_publish_empty_window();
            return 0U;
        }

        if (position >= music_track.Count)
        {
            /* 到列表末尾：本窗只有已拼好的这些行。 */
            catalog_track_diag_window_end();
            music_foreground_publish_window(music_fill.Generation, music_fill.Index, music_fill.Filled);
            return 0U;
        }

        row = &music_assembly.Rows[music_fill.Filled];
        if (found && music_row_cache_get(music_fill.Generation, position, row))
        {
            music_fill.Filled++;
            continue;
        }

        if (music_file_call_used)
        {
            return 0U; /* 本步的文件调用已经用掉：下一步接着拼这一行。 */
        }

        if (!found)
        {
            /* 单行映射失败（防御分支）：该行两列为空串，整窗继续。 */
            music_foreground_row_blank(row);
            music_fill.Filled++;
            continue;
        }

        music_file_call_used = true;
        music_foreground_apply_locator_hook(&music_track);
        start_cycles = catalog_track_diag_begin();
        result = catalog_track_text_read(position, &music_track);
        catalog_track_diag_row_done(start_cycles, result);

        if (result == NULL)
        {
            if (catalog_track_text_last_read_unavailable())
            {
                /* 读标签时卷不可用（拔卡的正常竞态，不是文件错误）：不填这一行、不发布，已发布的窗口保持不变（spec E）；
                 * 这一窗挂起，等卷事件（卷消失时曲库代次读作 0，下一步以空窗收尾）或新需求。前面已拼好的行留在组装缓冲里，不作废。 */
                music_fill.VolumeWait = true;
                return 0U;
            }

            /* 文件主名也提取不出（防御分支）：这一行两列为空串、不缓存，整窗继续。 */
            music_foreground_row_blank(row);
            music_fill.Filled++;
            return 0U;
        }

        if (result->OpenBusy)
        {
            music_foreground_note_busy(position);
            return MUSIC_BUSY_RETRY_MS; /* 文件槽用尽：不当文件错误，这一行过约 10 ms 重试，不设上限。 */
        }
        music_busy_streak = 0U;

        music_foreground_row_from_result(row, result);
        if (music_foreground_result_is_stable(result))
        {
            music_row_cache_put(music_fill.Generation, position, row);
        }
        music_fill.Filled++;

        if ((music_fill.Filled >= (uint16_t)MUSIC_WINDOW_MAX_ENTRIES) || ((uint32_t)position + 1U >= (uint32_t)music_track.Count))
        {
            catalog_track_diag_window_end();
            music_foreground_publish_window(music_fill.Generation, music_fill.Index, music_fill.Filled);
        }
        return 0U;
    }
}

/**
 * @brief 处理窗口需求：该发空窗就发；该拼就拼（至多读一行文件）；挂起等卷事件的一窗什么都不做。
 * @return SERVICE_BUSY 的重试等待（毫秒）；0 表示没有等待要求。
 */
static uint32_t music_foreground_process_window(void)
{
    const uint32_t generation = catalog_generation();

    if (!music_window_request.Valid)
    {
        return 0U;
    }

    if (music_fill.Active && ((generation != music_fill.Generation) || (music_window_request.Index != music_fill.Index)))
    {
        /* 代次变了：发空窗；起点变了（新需求覆盖旧需求）：丢掉没拼完的，从新起点重拼，已读的行靠行缓存不重读。 */
        const bool generation_changed = (generation != music_fill.Generation);

        music_fill.Active = false;
        music_fill.VolumeWait = false;
        catalog_track_diag_window_end();
        if (generation_changed)
        {
            music_foreground_publish_empty_window();
            return 0U;
        }
    }

    if (!music_fill.Active)
    {
        if (generation == 0U)
        {
            if (!(music_published.Valid && (music_published.Generation == 0U)))
            {
                music_foreground_publish_empty_window();
            }
            return 0U;
        }

        if (music_published.Valid && (music_published.Generation == generation) && (music_published.Index == music_window_request.Index))
        {
            return 0U; /* 窗口格里已经是这一窗。 */
        }

        music_foreground_begin_fill(generation);
    }

    if (music_fill.VolumeWait)
    {
        return 0U; /* 挂起等卷事件：代次和需求都没变，什么都不做（窗口格保持不变），睡下等通知。 */
    }

    return music_foreground_fill_rows();
}

/**
 * @brief 前台任务调用：做一小步。每步先看曲名，再最多读窗口的一行，然后重新看需求。
 * @return 多少毫秒后再叫它：0 马上（还有活）；`MUSIC_BUSY_RETRY_MS`（文件槽用尽，过约 10 ms 重试，不设上限）；1（需求读不到，
 *         锁被占着，尽快重试）；`FOREGROUND_WAIT_FOREVER`（没有活，或这一窗因卷不可用挂起，等卷事件或新需求的通知）。
 * @note  一步最多一次文件调用加约 1 ms CPU，不碰 Flash 卷（ARC-26）。曲名需求优先：它用掉本步的文件调用时窗口这一步不读行。
 *        曲库发布、卷就绪后主动重新处理最新需求：曲名看曲库代次，窗口比对窗口格里的代次，所以不需要 GUI 重写没变的需求。
 */
uint32_t music_foreground_step(void)
{
    uint32_t wait_ms = FOREGROUND_WAIT_FOREVER;
    uint32_t retry_ms;
    bool lock_busy;

    music_file_call_used = false;
    if (!music_announced)
    {
        music_announced = true;
        music_hook_pageturn_announce();
    }

    (void)music_foreground_read_requests();
    music_foreground_revisit_title();

    if (music_title_pending)
    {
        retry_ms = music_foreground_process_title();
        if (retry_ms != 0U)
        {
            wait_ms = retry_ms;
        }
    }

    retry_ms = music_foreground_process_window();
    if ((retry_ms != 0U) && (retry_ms < wait_ms))
    {
        wait_ms = retry_ms;
    }

    /* 然后重新看需求：这一步期间 GUI 又写了新的，马上再来一步，不睡。 */
    lock_busy = music_foreground_read_requests();
    if (lock_busy && (wait_ms > 1U))
    {
        wait_ms = 1U; /* 序号变了但锁被占着没读到：没有人再叫醒我们，所以自己 1 ms 后重试。 */
    }
    if ((wait_ms == FOREGROUND_WAIT_FOREVER) && (music_title_pending || music_foreground_window_needs_work()))
    {
        wait_ms = 0U;
    }

    {
        const uint32_t now_ms = (uint32_t)xTaskGetTickCount();
        const uint32_t hook_wait_ms = music_hook_pageturn_poll(now_ms);

        music_display_stats_poll(now_ms);
        if (hook_wait_ms < wait_ms)
        {
            wait_ms = hook_wait_ms;
        }
    }

    return wait_ms;
}
