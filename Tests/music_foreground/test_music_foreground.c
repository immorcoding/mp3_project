/**
 ******************************************************************************
 * @file    test_music_foreground.c
 * @brief   音乐前台流程的主机行为测试（STOR-6、STOR-15、STOR-16、ARC-26、ARC-27、#218）：
 *          覆盖、已读的行不重读、缓存命中不读文件、凑齐才发布、代次变化发空窗、失败不缓存、发布后重新发窗。
 *
 * @details
 *          链接真实的 `music_foreground.c`、`music_row_cache.c`、`music_cells.c`、`ui_cell.c`、共用 FreeRTOS 桩、
 *          显示延迟汇总与翻页钩子；曲库（`catalog_read_track()`、`catalog_generation()`）、曲目文本读取
 *          （`catalog_track_text_read()`）、读标签诊断、卷边沿和块池就绪数在本文件里用假实现替换，只实现既有公开函数。
 *          观察点都是公开的：GUI 一侧经 `ui_cell` 写需求、读结果（发布的界面数据），文件调用次数由假读取函数计数。
 *          期望值写成字面量（来自 #193、#202 E 节），不用代码里的表达式重算。
 *          主机测试是单线程的：证明分支走对了，不证明真实并发、持锁时长、与音频读取争 SD 锁的真实交错。
 ******************************************************************************
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include "APP/apps/music/catalog/catalog.h"
#include "APP/apps/music/catalog/catalog_track_diag.h"
#include "APP/apps/music/catalog/catalog_track_text.h"
#include "APP/apps/music/foreground/music_foreground.h"
#include "APP/apps/music/foreground/music_row_cache.h"
#include "APP/apps/music/music_display_stats.h"
#include "APP/tasks/audio_reader/audio_reader_config.h"
#include "APP/apps/music/music_cells.h"
#include "APP/tasks/foreground/foreground_task.h"
#include "APP/tasks/volume/volume_event.h"
#include "Service/log/log_service.h"

#define STEP_GUARD 400U /* 一个场景里最多调多少步；防止实现写成死循环。 */

/* ---- 假曲库 ---- */
static uint32_t g_generation;  /* 当前曲库代次；0 表示不可用（扫描中、没卡）。 */
static uint16_t g_count;       /* 当前曲库条数。 */
static uint32_t g_read_track_calls;

/* ---- 假曲目文本读取（文件调用） ---- */
typedef enum
{
    BEHAVIOR_TAGGED = 0,   /* 取到标签：稳定，可缓存。 */
    BEHAVIOR_NO_ID3V2,     /* 无标签：回退文件主名，稳定，可缓存。 */
    BEHAVIOR_OPEN_FAILED,  /* 打开失败（不是 BUSY）：回退文件主名，不缓存。 */
    BEHAVIOR_BUSY,         /* 打开得到 SERVICE_BUSY：瞬时，不缓存，过约 10 ms 重试。 */
    BEHAVIOR_READ_ERROR,   /* 读取错误：回退文件主名，不缓存。 */
    BEHAVIOR_UNAVAILABLE,  /* 卷不可用（拔卡的正常竞态）：返回空结果，不缓存。 */
    BEHAVIOR_DEFENSIVE_NULL /* 防御分支：文件主名也提取不出，返回空结果。 */
} Behavior;

static Behavior g_behavior[512];     /* 每个位置的脚本；默认 TAGGED。 */
static uint32_t g_busy_times[512];   /* BEHAVIOR_BUSY 的位置先报这么多次 BUSY，之后变成 TAGGED。 */
static uint32_t g_text_calls;        /* 文件调用（读标签）次数。 */
static uint16_t g_text_positions[STEP_GUARD * 2U]; /* 每次文件调用读的位置，按顺序。 */
static bool g_unavailable_flag;      /* 最近一次读取是不是因为卷不可用。 */
static void (*g_after_read_hook)(uint32_t calls_so_far); /* 每次读取之后调用，用来在填窗途中改曲库。 */
static CatalogTrackText_ResultTypeDef g_text_result;
static bool g_last_locator_zero;     /* 最近一次文件调用收到的定位符是否全零（全零表示“无定位符”）。 */

/* ---- 假诊断与观察 ---- */
static uint32_t g_diag_window_begin;
static uint32_t g_diag_window_end;
static uint32_t g_diag_row_done;
static uint32_t g_diag_name_done;
static uint32_t g_wake_calls;
static bool g_edge_available;
static uint32_t g_edge_tick;
static uint32_t g_ready_blocks;
static uint32_t g_free_blocks;
static SemaphoreHandle_t g_ui_cell_lock; /* ui_cell 模块唯一的锁；第一次初始化时创建，之后跨场景保留。 */

/**
 * @brief 假曲库的代次：测试脚本直接改 g_generation。
 * @return 当前代次。
 */
uint32_t catalog_generation(void)
{
    return g_generation;
}

/**
 * @brief 假读者一次读全：代次与条数；位置有效时给出可辨认的路径。
 * @param[in] position 播放列表位置。
 * @param[out] out 结果。
 * @return true 位置有效。
 */
bool catalog_read_track(uint16_t position, Catalog_TrackTypeDef *out)
{
    g_read_track_calls++;
    (void)memset(out, 0, sizeof(*out));
    out->Generation = g_generation;
    if (g_generation == 0U)
    {
        return false;
    }
    out->Count = g_count;
    if (position >= g_count)
    {
        return false;
    }
    (void)snprintf(out->Path, sizeof(out->Path), "Music/track%05u.mp3", (unsigned)position);
    out->Locator.Opaque[0] = 0x1000U + position; /* 非零：有定位符（内容对调用者不透明，假实现随便填）。 */
    out->Locator.Opaque[3] = 0x2000U + position;
    return true;
}

/**
 * @brief 假曲目文本读取：按脚本给出结果，并计一次文件调用。
 * @param[in] position 播放列表位置。
 * @param[in] track 一次读全的结果（只读路径与代次）。
 * @return 结果；脚本为卷不可用或防御分支时为 NULL。
 */
const CatalogTrackText_ResultTypeDef *catalog_track_text_read(uint16_t position, const Catalog_TrackTypeDef *track)
{
    const Behavior behavior = g_behavior[position];

    assert(track->Path[0] != '\0'); /* 只会为有效位置读文件。 */
    g_last_locator_zero = (track->Locator.Opaque[0] == 0U) && (track->Locator.Opaque[1] == 0U) &&
                          (track->Locator.Opaque[2] == 0U) && (track->Locator.Opaque[3] == 0U);
    assert(g_text_calls < (STEP_GUARD * 2U));
    g_text_positions[g_text_calls] = position;
    g_text_calls++;
    g_unavailable_flag = false;
    (void)memset(&g_text_result, 0, sizeof(g_text_result));
    (void)snprintf(g_text_result.Text.Title, sizeof(g_text_result.Text.Title), "Title %u g%lu", (unsigned)position,
                   (unsigned long)track->Generation);
    (void)snprintf(g_text_result.Text.Artist, sizeof(g_text_result.Text.Artist), "Artist %u", (unsigned)position);
    g_text_result.Text.Result = CATALOG_TEXT_TAGGED;

    switch (behavior)
    {
        case BEHAVIOR_NO_ID3V2:
            (void)snprintf(g_text_result.Text.Title, sizeof(g_text_result.Text.Title), "track%05u", (unsigned)position);
            g_text_result.Text.Artist[0] = '\0';
            g_text_result.Text.Result = CATALOG_TEXT_NO_ID3V2;
            break;
        case BEHAVIOR_OPEN_FAILED:
            (void)snprintf(g_text_result.Text.Title, sizeof(g_text_result.Text.Title), "track%05u", (unsigned)position);
            g_text_result.Text.Artist[0] = '\0';
            g_text_result.OpenFailed = true;
            g_text_result.Text.Result = CATALOG_TEXT_NO_ID3V2;
            break;
        case BEHAVIOR_BUSY:
            if (g_busy_times[position] != 0U)
            {
                g_busy_times[position]--;
                (void)snprintf(g_text_result.Text.Title, sizeof(g_text_result.Text.Title), "track%05u", (unsigned)position);
                g_text_result.Text.Artist[0] = '\0';
                g_text_result.OpenFailed = true;
                g_text_result.OpenBusy = true;
                g_text_result.Text.Result = CATALOG_TEXT_NO_ID3V2;
            }
            break;
        case BEHAVIOR_READ_ERROR:
            (void)snprintf(g_text_result.Text.Title, sizeof(g_text_result.Text.Title), "track%05u", (unsigned)position);
            g_text_result.Text.Artist[0] = '\0';
            g_text_result.Text.Result = CATALOG_TEXT_READ_ERROR;
            break;
        case BEHAVIOR_UNAVAILABLE:
            g_unavailable_flag = true;
            if (g_after_read_hook != NULL)
            {
                g_after_read_hook(g_text_calls);
            }
            return NULL;
        case BEHAVIOR_DEFENSIVE_NULL:
            if (g_after_read_hook != NULL)
            {
                g_after_read_hook(g_text_calls);
            }
            return NULL;
        case BEHAVIOR_TAGGED:
        default:
            break;
    }

    if (g_after_read_hook != NULL)
    {
        g_after_read_hook(g_text_calls);
    }
    return &g_text_result;
}

/**
 * @brief 假：最近一次读取是否因为卷不可用而返回空结果。
 * @return 脚本给出的标志。
 */
bool catalog_track_text_last_read_unavailable(void)
{
    return g_unavailable_flag;
}

/** @brief 假诊断：计时起点。 @return 0。 */
uint32_t catalog_track_diag_begin(void)
{
    return 0U;
}

/**
 * @brief 假诊断：曲名读取完成。
 * @param[in] index 播放列表位置。
 * @param[in] start_cycles 起点。
 * @param[in] track 读取结果。
 */
void catalog_track_diag_name_done(uint16_t index, uint32_t start_cycles, const CatalogTrackText_ResultTypeDef *track)
{
    (void)index;
    (void)start_cycles;
    (void)track;
    g_diag_name_done++;
}

/** @brief 假诊断：窗口开始。 */
void catalog_track_diag_window_begin(void)
{
    g_diag_window_begin++;
}

/**
 * @brief 假诊断：窗口一行读完。
 * @param[in] start_cycles 起点。
 * @param[in] track 读取结果。
 */
void catalog_track_diag_row_done(uint32_t start_cycles, const CatalogTrackText_ResultTypeDef *track)
{
    (void)start_cycles;
    (void)track;
    g_diag_row_done++;
}

/** @brief 假诊断：窗口结束。 */
void catalog_track_diag_window_end(void)
{
    g_diag_window_end++;
}

/**
 * @brief 假卷边沿时刻：脚本给出。
 * @param[in] volume 卷。
 * @param[out] tick 边沿 tick。
 * @return 脚本给出的是否读到。
 */
bool volume_event_published_edge(Volume_IdTypeDef volume, uint32_t *tick)
{
    (void)volume;
    if (g_edge_available)
    {
        *tick = g_edge_tick;
    }
    return g_edge_available;
}

/** @brief 假块池就绪数。 @return 脚本给出的就绪块数。 */
uint32_t audio_reader_block_ready_count(void)
{
    return g_ready_blocks;
}

/** @brief 假块池空闲数。 @return 脚本给出的空闲块数。 */
uint32_t audio_reader_block_free_count(void)
{
    return g_free_blocks;
}

/** @brief 假叫醒前台任务：计次（需求格的叫醒回调）。 */
void foreground_task_wake(void)
{
    g_wake_calls++;
}

/* ---- GUI 一侧的观察点：经 ui_cell 写需求、读结果 ---- */
static uint32_t g_gui_title_seen;
static uint32_t g_gui_window_seen;
static uint32_t g_title_sequence;

/**
 * @brief GUI 写曲名需求。
 * @param[in] generation 曲库代次，0 表示当前。
 * @param[in] index 播放列表位置。
 * @param[in] wanted 是否有需求。
 * @return 本次写入的需求序号。
 */
static uint32_t gui_write_title_request(uint32_t generation, uint16_t index, bool wanted)
{
    MusicCells_TitleRequestTypeDef request;

    request.Sequence = ++g_title_sequence;
    request.Generation = generation;
    request.Index = index;
    request.Wanted = wanted;
    assert(ui_cell_try_write(&music_cell_title_request, &request));
    return request.Sequence;
}

/**
 * @brief GUI 写窗口需求。
 * @param[in] index 窗口起点。
 */
static void gui_write_window_request(uint16_t index)
{
    MusicCells_WindowRequestTypeDef request;

    request.Index = index;
    request.Valid = true;
    assert(ui_cell_try_write(&music_cell_window_request, &request));
}

/**
 * @brief GUI 读窗口结果：只在有新的一份时返回真。
 * @param[out] out 窗口副本。
 * @return 是否有新的一份。
 */
static bool gui_read_window(MusicCells_WindowTypeDef *out)
{
    return ui_cell_read_if_changed(&music_cell_window_result, out, &g_gui_window_seen);
}

/**
 * @brief GUI 读曲名结果：只在有新的一份时返回真。
 * @param[out] out 曲名结果副本。
 * @return 是否有新的一份。
 */
static bool gui_read_title(MusicCells_TitleResultTypeDef *out)
{
    return ui_cell_read_if_changed(&music_cell_title_result, out, &g_gui_title_seen);
}

/**
 * @brief 调一步，并检查“一步最多一次文件调用”（ARC-26）。
 * @return 本步的返回值。
 */
static uint32_t step_checked(void)
{
    const uint32_t calls_before = g_text_calls;
    const uint32_t result = music_foreground_step();

    assert((g_text_calls - calls_before) <= 1U);
    return result;
}

/**
 * @brief 一直调步直到前台任务可以睡下（返回等通知）；返回调了多少步。
 * @return 步数。
 */
static uint32_t run_until_idle(void)
{
    uint32_t steps = 0U;

    while (step_checked() != FOREGROUND_WAIT_FOREVER)
    {
        steps++;
        assert(steps < STEP_GUARD);
    }
    return steps + 1U;
}

/**
 * @brief 开始一个场景：全部状态清零，曲库有 30 首、代次 5，读窗口结果与曲名结果的 GUI 基线对齐到初值。
 */
static void begin_scenario(void)
{
    MusicCells_WindowTypeDef window;
    MusicCells_TitleResultTypeDef title;

    freertos_stub_reset();
    lock_env_counter_reset();
    lock_env_log_reset();
    (void)memset(g_behavior, 0, sizeof(g_behavior));
    (void)memset(g_busy_times, 0, sizeof(g_busy_times));
    g_generation = 5U;
    g_count = 30U;
    g_read_track_calls = 0U;
    g_text_calls = 0U;
    g_unavailable_flag = false;
    g_after_read_hook = NULL;
    g_diag_window_begin = 0U;
    g_diag_window_end = 0U;
    g_diag_row_done = 0U;
    g_diag_name_done = 0U;
    g_wake_calls = 0U;
    g_edge_available = false;
    g_ready_blocks = 0U;
    g_free_blocks = 0U;
    g_title_sequence = 0U;
    g_gui_title_seen = 0U;
    g_gui_window_seen = 0U;
    music_cells_boot_init();
    music_foreground_boot_init();
    if (g_ui_cell_lock == NULL)
    {
        g_ui_cell_lock = freertos_stub_last_created_semaphore();
    }
    assert(g_ui_cell_lock != NULL);
    freertos_stub_semaphore_fail_next_takes(g_ui_cell_lock, 0U);
    /* 新读者拿到格子的初值一次：未发布的窗口、序号 0 的曲名结果。 */
    assert(gui_read_window(&window));
    assert(!window.Published);
    assert(gui_read_title(&title));
    assert(title.RequestSequence == 0U);
    g_wake_calls = 0U;
}

/**
 * @brief 定位符失效钩子（R5.5，默认关闭）：默认构建里前台把曲库读出的定位符原样交给文件读取；钩子打开的构建
 *        （`AUDIO_READER_HOOK_LOCATOR_MISS=1`，同一份测试源另编一个配置）里曲名和窗口行都把定位符清零，走“退回按路径打开”。
 */
static void test_the_locator_miss_hook_only_changes_the_locator_handed_to_the_file_read(void)
{
    begin_scenario();
    (void)gui_write_title_request(0U, 3U, true);
    (void)run_until_idle();
    assert(g_text_calls == 1U);
    assert(g_last_locator_zero == (AUDIO_READER_HOOK_LOCATOR_MISS != 0U));

    gui_write_window_request(10U);
    (void)run_until_idle();
    assert(g_text_calls == 13U);
    assert(g_last_locator_zero == (AUDIO_READER_HOOK_LOCATOR_MISS != 0U));
}

/** @brief 没有需求时什么都不做：不读文件、不发布，前台任务可以一直睡。 */
static void test_without_any_request_nothing_happens(void)
{
    MusicCells_WindowTypeDef window;
    MusicCells_TitleResultTypeDef title;

    begin_scenario();
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(g_text_calls == 0U);
    assert(!gui_read_window(&window));
    assert(!gui_read_title(&title));
}

/** @brief 需求格写入会叫醒前台任务（一个通用的“需求变了”通知）；结果格不叫醒。 */
static void test_writing_a_request_wakes_the_foreground_but_publishing_a_result_does_not(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    assert(g_wake_calls == 1U);
    gui_write_title_request(0U, 3U, true);
    assert(g_wake_calls == 2U);

    g_wake_calls = 0U;
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(g_wake_calls == 0U);
}

/** @brief 凑齐才发布：十二行没读完时 GUI 看不到半窗；第十二行读完的那一步才发布整窗，内容与顺序正确。 */
static void test_a_window_is_published_only_when_all_rows_are_ready(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);

    for (uint32_t step = 1U; step < 12U; step++)
    {
        (void)step_checked();
        assert(g_text_calls == step);
        assert(!gui_read_window(&window)); /* 还差几行：GUI 什么也看不到。 */
    }

    assert(step_checked() == FOREGROUND_WAIT_FOREVER); /* 第十二行读完就发布，前台没有别的事了。 */
    assert(g_text_calls == 12U);
    assert(gui_read_window(&window));
    assert(window.Published);
    assert(window.Generation == 5U);
    assert(window.Index == 0U);
    assert(window.Length == 12U);
    for (uint16_t row = 0U; row < 12U; row++)
    {
        char expected_title[32];
        char expected_artist[32];

        (void)snprintf(expected_title, sizeof(expected_title), "Title %u g5", (unsigned)row);
        (void)snprintf(expected_artist, sizeof(expected_artist), "Artist %u", (unsigned)row);
        assert(strcmp(window.Rows[row].Title, expected_title) == 0);
        assert(strcmp(window.Rows[row].Artist, expected_artist) == 0);
        assert(g_text_positions[row] == row);
    }
    assert(g_diag_window_begin == 1U);
    assert(g_diag_window_end == 1U);
    assert(g_diag_row_done == 12U);
}

/** @brief 列表末尾：起点 25、共 30 首，只有 5 行，读完最后一行就发布，条数是实际条数。 */
static void test_the_last_window_holds_only_the_rows_that_exist(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(25U);
    (void)run_until_idle();

    assert(g_text_calls == 5U);
    assert(gui_read_window(&window));
    assert(window.Index == 25U);
    assert(window.Length == 5U);
    assert(window.Generation == 5U);
    assert(strcmp(window.Rows[4].Title, "Title 29 g5") == 0);
}

/** @brief 起点越过列表末尾：发布条数 0 的窗口（代次仍是当前代次，不是空窗）。 */
static void test_a_window_starting_beyond_the_list_is_published_with_no_rows(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(40U);
    (void)run_until_idle();

    assert(g_text_calls == 0U);
    assert(gui_read_window(&window));
    assert(window.Published);
    assert(window.Generation == 5U);
    assert(window.Length == 0U);
}

/** @brief 新需求覆盖旧需求：旧起点读到一半时来了新起点，丢掉旧的从新起点重拼，只发布新起点的窗口。 */
static void test_a_newer_window_request_overrides_the_older_one(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    (void)step_checked();
    (void)step_checked();
    (void)step_checked();
    assert(g_text_calls == 3U);
    assert(!gui_read_window(&window));

    gui_write_window_request(20U);
    (void)run_until_idle();

    assert(gui_read_window(&window));
    assert(window.Index == 20U);
    assert(window.Length == 10U);
    assert(!gui_read_window(&window)); /* 起点 0 的窗口从没发布过。 */
    assert(g_text_positions[3] == 20U);
}

/** @brief 已读的行不重读：起点 0 读完后起点移到 6，第 6 至 11 行来自缓存，只读新的 6 行。 */
static void test_rows_already_read_are_not_read_again(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    (void)run_until_idle();
    assert(g_text_calls == 12U);
    assert(gui_read_window(&window));

    gui_write_window_request(6U);
    (void)run_until_idle();

    assert(g_text_calls == 18U); /* 只多读了第 12 至 17 行。 */
    assert(g_text_positions[12] == 12U);
    assert(g_text_positions[17] == 17U);
    assert(gui_read_window(&window));
    assert(window.Index == 6U);
    assert(window.Length == 12U);
    assert(strcmp(window.Rows[0].Title, "Title 6 g5") == 0);   /* 来自缓存。 */
    assert(strcmp(window.Rows[11].Title, "Title 17 g5") == 0); /* 新读的。 */
}

/** @brief 缓存命中不读文件：整窗都在缓存里时，再要同一窗一次文件调用也没有，而且一步就发布。 */
static void test_a_fully_cached_window_is_republished_without_any_file_call(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    (void)run_until_idle();
    gui_write_window_request(12U);
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(g_text_calls == 24U);

    gui_write_window_request(0U);
    assert(step_checked() == FOREGROUND_WAIT_FOREVER); /* 一步就发布，不读文件。 */
    assert(g_text_calls == 24U);
    assert(gui_read_window(&window));
    assert(window.Index == 0U);
    assert(window.Length == 12U);
    assert(strcmp(window.Rows[7].Title, "Title 7 g5") == 0);
}

/** @brief 缓存命中的行不用读文件就能答曲名需求；曲名需求读过的行，窗口也不用再读。曲名与窗口共用一份缓存。 */
static void test_title_and_window_share_the_row_cache(void)
{
    MusicCells_WindowTypeDef window;
    MusicCells_TitleResultTypeDef title;
    uint32_t sequence;

    begin_scenario();
    gui_write_window_request(0U);
    (void)run_until_idle();
    assert(g_text_calls == 12U);

    sequence = gui_write_title_request(0U, 4U, true); /* 第 4 行已在窗口读过：不读文件。 */
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(g_text_calls == 12U);
    assert(gui_read_title(&title));
    assert(title.RequestSequence == sequence);
    assert(title.Generation == 5U);
    assert(strcmp(title.Title, "Title 4 g5") == 0);
    assert(strcmp(title.Artist, "Artist 4") == 0);

    sequence = gui_write_title_request(0U, 20U, true); /* 第 20 行没读过：读一次。 */
    (void)run_until_idle();
    assert(g_text_calls == 13U);
    assert(g_text_positions[12] == 20U);
    assert(gui_read_title(&title));
    assert(title.RequestSequence == sequence);
    assert(strcmp(title.Title, "Title 20 g5") == 0);

    gui_write_window_request(14U); /* 窗口 14 至 25 里第 20 行已缓存，只读另外 11 行。 */
    (void)run_until_idle();
    assert(g_text_calls == 24U);
    assert(gui_read_window(&window));
    assert(strcmp(window.Rows[6].Title, "Title 20 g5") == 0);
}

/** @brief 每轮先看曲名，再读窗口的一行：曲名和窗口同时有需求时，第一步的那次文件调用读曲名，窗口排在后面。 */
static void test_the_title_is_looked_at_before_the_window_in_every_step(void)
{
    MusicCells_WindowTypeDef window;
    MusicCells_TitleResultTypeDef title;

    begin_scenario();
    gui_write_window_request(0U);
    gui_write_title_request(0U, 17U, true);

    assert(step_checked() == 0U);   /* 第一步：曲名用掉唯一一次文件调用，窗口还要继续。 */
    assert(g_text_calls == 1U);
    assert(g_text_positions[0] == 17U);
    assert(gui_read_title(&title));
    assert(!gui_read_window(&window));

    (void)step_checked();           /* 第二步：窗口读第 0 行。 */
    assert(g_text_calls == 2U);
    assert(g_text_positions[1] == 0U);
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Length == 12U);
}

/** @brief 曲名结果带回所答的需求序号和实际代次；连写几次需求只答最新的一次。 */
static void test_the_title_result_echoes_the_request_sequence_and_only_the_latest_request_is_answered(void)
{
    MusicCells_TitleResultTypeDef title;
    uint32_t last_sequence = 0U;

    begin_scenario();
    (void)gui_write_title_request(0U, 3U, true);
    (void)gui_write_title_request(0U, 4U, true);
    last_sequence = gui_write_title_request(0U, 5U, true);
    (void)run_until_idle();

    assert(g_text_calls == 1U);
    assert(g_text_positions[0] == 5U);
    assert(gui_read_title(&title));
    assert(title.RequestSequence == last_sequence);
    assert(title.Generation == 5U);
    assert(strcmp(title.Title, "Title 5 g5") == 0);
    assert(!gui_read_title(&title)); /* 只发布了一次。 */
    assert(g_diag_name_done == 1U);
}

/** @brief 曲名需求的代次 0 表示当前；写了确切代次且相符同样答；不符答代次 0 空名，不读文件。 */
static void test_title_generation_zero_means_current_and_a_mismatch_answers_zero(void)
{
    MusicCells_TitleResultTypeDef title;
    uint32_t sequence;

    begin_scenario();
    sequence = gui_write_title_request(5U, 2U, true);
    (void)run_until_idle();
    assert(gui_read_title(&title));
    assert(title.RequestSequence == sequence);
    assert(title.Generation == 5U);
    assert(strcmp(title.Title, "Title 2 g5") == 0);

    sequence = gui_write_title_request(4U, 2U, true); /* 旧代次。 */
    (void)run_until_idle();
    assert(g_text_calls == 1U); /* 上面那次读了一次；这一次没有再读。 */
    assert(gui_read_title(&title));
    assert(title.RequestSequence == sequence);
    assert(title.Generation == 0U);
    assert(title.Title[0] == '\0');
    assert(title.Artist[0] == '\0');
    assert(lock_env_log_count() == 0U); /* 代次不符是重扫与拔卡时的正常竞态，不记日志。 */
}

/** @brief 越界：请求“当前”时越界是空库开播的正常过程，不记；请求了确切代次却越界是防御分支，记一条 WARN。 */
static void test_an_out_of_range_title_is_silent_for_current_and_warns_for_an_explicit_generation(void)
{
    MusicCells_TitleResultTypeDef title;

    begin_scenario();
    (void)gui_write_title_request(0U, 99U, true);
    (void)run_until_idle();
    assert(gui_read_title(&title));
    assert(title.Generation == 0U);
    assert(lock_env_log_count() == 0U);

    (void)gui_write_title_request(5U, 99U, true);
    (void)run_until_idle();
    assert(gui_read_title(&title));
    assert(title.Generation == 0U);
    assert(lock_env_log_count() == 1U);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_WARN);
    assert(strcmp(lock_env_log_text(0U), "Name fail idx=99 gen=5") == 0);
    assert(g_text_calls == 0U);
}

/** @brief 没有需求（没有当前曲）：不读文件，也不发布。 */
static void test_a_title_request_that_is_not_wanted_reads_nothing(void)
{
    MusicCells_TitleResultTypeDef title;

    begin_scenario();
    (void)gui_write_title_request(0U, 3U, false);
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(g_text_calls == 0U);
    assert(!gui_read_title(&title));
}

/** @brief 代次变化发空窗：读到一半曲库重扫了，发布代次 0 的空窗，不发半窗。 */
static void test_a_generation_change_during_a_fill_publishes_an_empty_window(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    (void)step_checked();
    (void)step_checked();
    (void)step_checked();
    assert(!gui_read_window(&window));

    g_generation = 6U; /* 重扫发布了新曲库。 */
    (void)step_checked();

    assert(gui_read_window(&window));
    assert(window.Published);
    assert(window.Generation == 0U);
    assert(window.Length == 0U);
}

/** @brief 发布后重新发窗：空窗发出后不需要 GUI 重写需求，前台按最新需求用新代次重拼整窗，旧代次的缓存行不命中。 */
static void test_after_the_empty_window_the_latest_request_is_served_again_without_a_new_request(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    for (uint32_t step = 0U; step < 4U; step++)
    {
        (void)step_checked();
    }
    g_generation = 6U;
    (void)run_until_idle();

    assert(gui_read_window(&window)); /* 读到的是最新的一份：新代次的整窗。 */
    assert(window.Generation == 6U);
    assert(window.Index == 0U);
    assert(window.Length == 12U);
    assert(strcmp(window.Rows[0].Title, "Title 0 g6") == 0);
    assert(g_text_calls == 4U + 12U); /* 旧代次读过的 4 行不能用，全部重读。 */
}

/** @brief 空窗是一份独立的发布：代次变化时先有一份代次 0 的空窗，再有新代次的整窗，GUI 经序号都能看到。 */
static void test_the_empty_window_and_the_new_window_are_two_separate_publications(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    (void)step_checked();
    g_generation = 6U;

    (void)step_checked(); /* 发空窗，并且前台还有活（重新处理需求），所以返回 0。 */
    assert(gui_read_window(&window));
    assert(window.Generation == 0U);
    assert(step_checked() == 0U);
    assert(!gui_read_window(&window)); /* 新窗口要等读完。 */
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Generation == 6U);
}

/** @brief 卡拔走（曲库代次 0）：发布一份空窗，之后不再重复发布；卡回来（新代次）后按同一个需求发整窗。 */
static void test_card_removal_publishes_one_empty_window_and_the_card_back_publishes_the_window_again(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Generation == 5U);

    g_generation = 0U; /* 拔卡：曲库代次读作 0。 */
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(gui_read_window(&window));
    assert(window.Published);
    assert(window.Generation == 0U);
    assert(window.Length == 0U);

    for (uint32_t i = 0U; i < 5U; i++)
    {
        assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    }
    assert(!gui_read_window(&window)); /* 没有重复发布。 */
    assert(g_text_calls == 12U);

    g_generation = 7U; /* 重新插卡、扫描发布。 */
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Generation == 7U);
    assert(window.Length == 12U);
    assert(g_text_calls == 24U); /* 新卡的行全部重读。 */
}

/** @brief 开机时曲库还没好（代次 0）：发一份代次 0 的空窗，然后睡；曲库发布后再发整窗，不需要新需求。 */
static void test_the_catalog_not_ready_yet_gets_an_empty_window_and_the_publish_gets_the_real_one(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    g_generation = 0U;
    gui_write_window_request(0U);
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(gui_read_window(&window));
    assert(window.Published);
    assert(window.Generation == 0U);
    assert(g_text_calls == 0U);

    g_generation = 1U; /* 曲库发布（catalog.c 随后叫醒前台任务）。 */
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Generation == 1U);
    assert(window.Length == 12U);
}

/** @brief 因卷变化而发空窗时，记下卡检测边沿时刻供“拔卡后 Queue 清空”延迟用：GUI 清空 Queue 时算差，汇总行里 clear 一类出现这次样本。 */
static void test_the_empty_window_notes_the_volume_edge_for_the_clear_latency(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    (void)run_until_idle();
    assert(gui_read_window(&window));

    g_edge_available = true;
    g_edge_tick = 4321U;
    g_generation = 0U;
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Generation == 0U);

    music_display_stats_note_clear(4400U); /* GUI 在 4400 ms 套用空窗、清空 Queue。 */
    music_display_stats_poll(10000U);
    assert(lock_env_log_count() == 1U);
    assert(strstr(lock_env_log_text(0U), "clear n=1 max=79ms") != NULL);
}

/**
 * @brief 把某个位置的脚本设成 behavior，读一窗，再恢复正常、再要同一窗：失败的行第二次必须重新读文件，好的行不读。
 * @param[in] behavior 第 5 行的脚本。
 * @param[out] first_window 第一次发布的窗口。
 * @param[out] second_window 第二次发布的窗口。
 */
static void read_window_twice_with_row_five(Behavior behavior, MusicCells_WindowTypeDef *first_window, MusicCells_WindowTypeDef *second_window)
{
    begin_scenario();
    g_behavior[5] = behavior;
    gui_write_window_request(0U);
    (void)run_until_idle();
    assert(gui_read_window(first_window));
    assert(first_window->Length == 12U);
    assert(g_text_calls == 12U);

    g_behavior[5] = BEHAVIOR_TAGGED;
    gui_write_window_request(1U); /* 起点挪一下，让前台重新拼：第 1 至 12 行，第 5 行在里面。 */
    (void)run_until_idle();
    assert(gui_read_window(second_window));
}

/** @brief 失败不缓存：打开失败的行显示回退的文件主名，但不进缓存，下一窗重新读到正确的标签。 */
static void test_an_open_failure_shows_the_fallback_name_but_is_not_cached(void)
{
    MusicCells_WindowTypeDef first;
    MusicCells_WindowTypeDef second;

    read_window_twice_with_row_five(BEHAVIOR_OPEN_FAILED, &first, &second);

    assert(strcmp(first.Rows[5].Title, "track00005") == 0);
    assert(g_text_calls == 12U + 2U);       /* 第 5 行重读一次，第 12 行新读一次，其余来自缓存。 */
    assert(g_text_positions[12] == 5U);
    assert(g_text_positions[13] == 12U);
    assert(strcmp(second.Rows[4].Title, "Title 5 g5") == 0); /* 窗口起点 1：第 5 行在下标 4。 */
}

/** @brief 失败不缓存：读取错误同样不缓存。 */
static void test_a_read_error_is_not_cached(void)
{
    MusicCells_WindowTypeDef first;
    MusicCells_WindowTypeDef second;

    read_window_twice_with_row_five(BEHAVIOR_READ_ERROR, &first, &second);

    assert(strcmp(first.Rows[5].Title, "track00005") == 0);
    assert(g_text_calls == 12U + 2U);
    assert(strcmp(second.Rows[4].Title, "Title 5 g5") == 0);
}

/**
 * @brief 卷不可用（spec E“暂时不能读时窗口不变”）：第 12 行读到卷不可用时，不填这一行、不发布带空行的整窗，已发布的窗口保持不变；
 *        这一窗挂起，睡下等卷事件，不空转、不再调文件接口；卷没了（曲库代次 0）才发一份空窗，卷回来（新代次）再发真窗口。
 */
static void test_the_volume_going_unavailable_on_the_twelfth_row_publishes_nothing_and_keeps_the_window(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    (void)run_until_idle();
    assert(gui_read_window(&window)); /* 先正常发布窗口 0：代次 5，起点 0，12 行。 */
    assert(window.Generation == 5U);
    assert(window.Index == 0U);
    assert(g_text_calls == 12U);

    g_behavior[12] = BEHAVIOR_UNAVAILABLE; /* 窗口 1 的位置 1 至 11 来自缓存，第 12 行（位置 12）要读文件。 */
    gui_write_window_request(1U);
    (void)run_until_idle();

    assert(g_text_calls == 13U);        /* 只试读了位置 12 一次。 */
    assert(g_text_positions[12] == 12U);
    assert(!gui_read_window(&window));  /* 没有发布：不发带空行的整窗。 */
    assert(lock_env_log_count() == 0U); /* 卷不可用不是文件错误，不记日志。 */

    /* 挂起期间：睡下等通知，不空转、不再碰文件接口。 */
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(g_text_calls == 13U);
    assert(!gui_read_window(&window));

    /* 卷消失（卷事件，曲库代次读作 0）：发一份代次 0 的空窗收尾，只发一次。 */
    g_generation = 0U;
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Generation == 0U);
    assert(window.Length == 0U);
    assert(!gui_read_window(&window));
    assert(g_text_calls == 13U);

    /* 卡回来、曲库重新发布（新代次）：按最新需求重拼窗口 1，12 行都是新代次的真内容。 */
    g_generation = 6U;
    g_behavior[12] = BEHAVIOR_TAGGED;
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Generation == 6U);
    assert(window.Index == 1U);
    assert(window.Length == 12U);
    assert(strcmp(window.Rows[0].Title, "Title 1 g6") == 0);
    assert(strcmp(window.Rows[11].Title, "Title 12 g6") == 0);
}

/**
 * @brief 卷不可用发生在窗口中间：已拼好的行留在组装缓冲里，不发半窗；卷不可用的行不缓存；新需求（同一代次）从新起点重拼，
 *        这一行恢复可读后读到正确的标签。
 */
static void test_a_volume_unavailable_row_in_the_middle_waits_and_is_not_cached(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    g_behavior[5] = BEHAVIOR_UNAVAILABLE;
    gui_write_window_request(0U);
    (void)run_until_idle();

    assert(g_text_calls == 6U);        /* 位置 0 至 5，位置 5 卷不可用。 */
    assert(!gui_read_window(&window)); /* 没有半窗。 */
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(g_text_calls == 6U);

    /* 同一代次、新需求：挂起的一窗作废，从新起点重拼；位置 3、4 来自缓存，位置 5 仍卷不可用：再挂起一次。 */
    gui_write_window_request(3U);
    (void)run_until_idle();
    assert(g_text_calls == 7U);
    assert(g_text_positions[6] == 5U);
    assert(!gui_read_window(&window));

    /* 位置 5 恢复可读；再来一个新需求：位置 5 重新读文件（上一次卷不可用没有缓存），读到正确标签后整窗发布。 */
    g_behavior[5] = BEHAVIOR_TAGGED;
    gui_write_window_request(4U);
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Generation == 5U);
    assert(window.Index == 4U);
    assert(window.Length == 12U);
    assert(strcmp(window.Rows[1].Title, "Title 5 g5") == 0); /* 窗口起点 4：位置 5 在下标 1。 */
    assert(g_text_positions[7] == 5U);
}

/** @brief 稳定结果缓存：取到标签、无 ID3v2 的行都进缓存，第二次不重读。 */
static void test_stable_results_are_cached(void)
{
    MusicCells_WindowTypeDef first;
    MusicCells_WindowTypeDef second;

    read_window_twice_with_row_five(BEHAVIOR_NO_ID3V2, &first, &second);

    assert(strcmp(first.Rows[5].Title, "track00005") == 0);
    assert(g_text_calls == 12U + 1U);       /* 只新读第 12 行；第 5 行（无标签，稳定）来自缓存。 */
    assert(strcmp(second.Rows[4].Title, "track00005") == 0);
}

/** @brief 文件主名也提取不出（防御分支）：该行两列为空串，整窗继续，不缓存。 */
static void test_a_defensive_failure_leaves_the_row_blank_without_stopping_the_window(void)
{
    MusicCells_WindowTypeDef first;
    MusicCells_WindowTypeDef second;

    read_window_twice_with_row_five(BEHAVIOR_DEFENSIVE_NULL, &first, &second);

    assert(first.Rows[5].Title[0] == '\0');
    assert(first.Rows[6].Title[0] != '\0');
    assert(g_text_calls == 12U + 2U);
    assert(strcmp(second.Rows[4].Title, "Title 5 g5") == 0);
}

/** @brief SERVICE_BUSY 不缓存、过约 10 ms 重试、不算文件错误：这一步返回 10，行号不前进，没有显示回退名；槽空出来后读到正确的标签。 */
static void test_a_busy_open_is_retried_after_about_10_ms_and_never_shows_the_fallback_name(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    g_behavior[3] = BEHAVIOR_BUSY;
    g_busy_times[3] = 2U;
    gui_write_window_request(0U);

    (void)step_checked();
    (void)step_checked();
    (void)step_checked();
    assert(g_text_calls == 3U);           /* 第 0 至 2 行。 */
    assert(step_checked() == 10U);        /* 第 3 行 BUSY：过约 10 ms 重试。 */
    assert(g_text_calls == 4U);
    assert(step_checked() == 10U);        /* 第二次 BUSY。 */
    assert(g_text_calls == 5U);
    assert(!gui_read_window(&window));

    (void)run_until_idle();               /* 第三次成功，其余行读完。 */
    assert(gui_read_window(&window));
    assert(strcmp(window.Rows[3].Title, "Title 3 g5") == 0);
    assert(g_text_positions[3] == 3U);
    assert(g_text_positions[4] == 3U);
    assert(g_text_positions[5] == 3U);
    assert(g_text_positions[6] == 4U);
    assert(lock_env_log_count() == 1U);   /* 连续 BUSY 只在第 1 次记一行 WARN，之后不刷屏；BUSY 不是文件错误。 */
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_WARN);
    assert(strcmp(lock_env_log_text(0U), "SERVICE_BUSY pos=3 n=1") == 0);
}

/** @brief BUSY 之后读到的好结果会进缓存（BUSY 本身不缓存，成功才缓存）。 */
static void test_the_result_after_a_busy_retry_is_cached(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    g_behavior[3] = BEHAVIOR_BUSY;
    g_busy_times[3] = 1U;
    gui_write_window_request(0U);
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(g_text_calls == 13U); /* 12 行加 1 次 BUSY。 */

    gui_write_window_request(1U);
    (void)run_until_idle();
    assert(g_text_calls == 14U); /* 只新读第 12 行。 */
}

/** @brief 曲名需求遇到 BUSY：不发布结果（GUI 继续留空），过约 10 ms 重试，读到后才发布。 */
static void test_a_busy_title_is_retried_and_only_the_good_answer_is_published(void)
{
    MusicCells_TitleResultTypeDef title;

    begin_scenario();
    g_behavior[7] = BEHAVIOR_BUSY;
    g_busy_times[7] = 2U;
    (void)gui_write_title_request(0U, 7U, true);

    assert(step_checked() == 10U);
    assert(!gui_read_title(&title));
    assert(step_checked() == 10U);
    assert(!gui_read_title(&title));
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(gui_read_title(&title));
    assert(strcmp(title.Title, "Title 7 g5") == 0);
    assert(g_text_calls == 3U);
}

/**
 * @brief BUSY 一律重试，没有上限（ARC-27、spec“过约 10 ms 重试，不算失败”）：同一行连续 BUSY 250 次，每次都只返回 10 ms，
 *        从不显示回退名，槽空出来后读到正确的标签；日志只在第 1、100、200 次各一行，不刷屏。
 */
static void test_busy_is_retried_without_an_upper_bound_and_the_log_is_throttled(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    g_behavior[2] = BEHAVIOR_BUSY;
    g_busy_times[2] = 250U;
    gui_write_window_request(0U);

    (void)step_checked();
    (void)step_checked();
    for (uint32_t retry = 0U; retry < 250U; retry++)
    {
        assert(step_checked() == 10U); /* 第 2 行：每次都是 BUSY，过约 10 ms 重试。 */
        assert(!gui_read_window(&window));
    }
    assert(g_text_calls == 2U + 250U);
    assert(lock_env_log_count() == 3U);
    assert(strcmp(lock_env_log_text(0U), "SERVICE_BUSY pos=2 n=1") == 0);
    assert(strcmp(lock_env_log_text(1U), "SERVICE_BUSY pos=2 n=100") == 0);
    assert(strcmp(lock_env_log_text(2U), "SERVICE_BUSY pos=2 n=200") == 0);

    (void)run_until_idle();               /* 槽空出来：读到正确的标签，其余行读完。 */
    assert(gui_read_window(&window));
    assert(window.Length == 12U);
    assert(strcmp(window.Rows[2].Title, "Title 2 g5") == 0); /* 没有回退成文件主名 track00002。 */
    assert(lock_env_log_count() == 3U);   /* 恢复之后不再记。 */
}

/** @brief 曲名同样一律重试：BUSY 150 次之后读到的仍是正确的标签，不显示回退名。 */
static void test_a_title_busy_is_also_retried_without_an_upper_bound(void)
{
    MusicCells_TitleResultTypeDef title;

    begin_scenario();
    g_behavior[7] = BEHAVIOR_BUSY;
    g_busy_times[7] = 150U;
    (void)gui_write_title_request(0U, 7U, true);

    for (uint32_t retry = 0U; retry < 150U; retry++)
    {
        assert(step_checked() == 10U);
        assert(!gui_read_title(&title));
    }
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(gui_read_title(&title));
    assert(strcmp(title.Title, "Title 7 g5") == 0);
    assert(lock_env_log_count() == 2U); /* 第 1 次与第 100 次。 */
    assert(strcmp(lock_env_log_text(1U), "SERVICE_BUSY pos=7 n=100") == 0);
}

/** @brief 一直 BUSY 也会终止：曲库代次变了（卷事件）就以空窗收尾，不再重试，也不会空转。 */
static void test_a_persistent_busy_ends_when_the_catalog_generation_changes(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    g_behavior[2] = BEHAVIOR_BUSY;
    g_busy_times[2] = 100000U;
    gui_write_window_request(0U);

    (void)step_checked();
    (void)step_checked();
    assert(step_checked() == 10U);
    assert(step_checked() == 10U);

    g_generation = 0U; /* 卷消失：曲库读作代次 0。 */
    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Generation == 0U);
    assert(window.Length == 0U);
    assert(g_text_calls == 4U); /* 之后不再碰文件接口。 */
}

/** @brief 曲库不可用时曲名答代次 0；曲库发布后前台主动再答一次，GUI 不用重写需求，结果带回同一个序号。 */
static void test_a_title_asked_before_the_catalog_was_ready_is_answered_again_when_it_is_published(void)
{
    MusicCells_TitleResultTypeDef title;
    uint32_t sequence;

    begin_scenario();
    g_generation = 0U;
    sequence = gui_write_title_request(0U, 2U, true);
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
    assert(gui_read_title(&title));
    assert(title.RequestSequence == sequence);
    assert(title.Generation == 0U);

    g_generation = 1U; /* 曲库发布。 */
    (void)run_until_idle();
    assert(gui_read_title(&title));
    assert(title.RequestSequence == sequence);
    assert(title.Generation == 1U);
    assert(strcmp(title.Title, "Title 2 g1") == 0);
}

/** @brief 读标签时卷不可用：曲名答代次 0 空名，不记 WARN，不缓存。 */
static void test_a_title_read_with_the_volume_gone_answers_zero_without_a_warning(void)
{
    MusicCells_TitleResultTypeDef title;

    begin_scenario();
    g_behavior[4] = BEHAVIOR_UNAVAILABLE;
    (void)gui_write_title_request(0U, 4U, true);
    (void)run_until_idle();

    assert(gui_read_title(&title));
    assert(title.Generation == 0U);
    assert(title.Title[0] == '\0');
    assert(lock_env_log_count() == 0U);

    g_behavior[4] = BEHAVIOR_TAGGED;
    (void)gui_write_title_request(0U, 4U, true);
    (void)run_until_idle();
    assert(g_text_calls == 2U); /* 没有缓存失败的结果：重新读了文件。 */
    assert(gui_read_title(&title));
    assert(strcmp(title.Title, "Title 4 g5") == 0);
}

/** @brief 防御分支（文件主名也提取不出）：曲名答代次 0，记一条 WARN。 */
static void test_a_defensive_title_failure_warns(void)
{
    MusicCells_TitleResultTypeDef title;

    begin_scenario();
    g_behavior[4] = BEHAVIOR_DEFENSIVE_NULL;
    (void)gui_write_title_request(0U, 4U, true);
    (void)run_until_idle();

    assert(gui_read_title(&title));
    assert(title.Generation == 0U);
    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_text(0U), "Name fail idx=4 gen=0") == 0);
}

/** @brief 曲名读到失败结果（打开失败）显示回退文件主名但不缓存：再要同一首重新读。 */
static void test_a_title_read_failure_shows_the_fallback_and_is_not_cached(void)
{
    MusicCells_TitleResultTypeDef title;

    begin_scenario();
    g_behavior[6] = BEHAVIOR_OPEN_FAILED;
    (void)gui_write_title_request(0U, 6U, true);
    (void)run_until_idle();
    assert(gui_read_title(&title));
    assert(strcmp(title.Title, "track00006") == 0);

    g_behavior[6] = BEHAVIOR_TAGGED;
    (void)gui_write_title_request(0U, 6U, true);
    (void)run_until_idle();
    assert(g_text_calls == 2U);
    assert(gui_read_title(&title));
    assert(strcmp(title.Title, "Title 6 g5") == 0);
}

/** @brief 需求读不到（锁被别的任务占着）时不能睡下：序号变了但读失败，这一步返回 1 ms 后重试，之后读到并处理。 */
static void test_a_request_that_could_not_be_read_because_of_the_lock_is_retried_soon(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    assert(step_checked() == FOREGROUND_WAIT_FOREVER); /* 先把两个需求格的初值各读一次，之后每步只有窗口格会变。 */
    gui_write_window_request(0U);
    freertos_stub_semaphore_fail_next_takes(g_ui_cell_lock, 2U); /* 一步里窗口格要读两次（开头和结尾），两次都读不到。 */
    assert(step_checked() == 1U); /* 序号变了但读不到：1 ms 后再来，不是等通知（没有人再叫醒前台任务）。 */
    assert(g_text_calls == 0U);

    (void)run_until_idle();
    assert(gui_read_window(&window));
    assert(window.Length == 12U);
}

/** @brief 空闲时返回等通知，有活时返回 0：前台任务据此在没有需求时一直睡。 */
static void test_an_idle_step_waits_for_a_notification_and_a_busy_step_asks_to_be_called_again(void)
{
    begin_scenario();
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);

    gui_write_window_request(0U);
    assert(step_checked() == 0U);
    (void)run_until_idle();
    assert(step_checked() == FOREGROUND_WAIT_FOREVER);
}

/** @brief 启动时清零：重新启动之后（键表清零、状态复位）旧缓存不命中，需求要重新处理。 */
static void test_boot_init_forgets_cached_rows_and_pending_work(void)
{
    MusicCells_WindowTypeDef window;

    begin_scenario();
    gui_write_window_request(0U);
    (void)run_until_idle();
    assert(g_text_calls == 12U);

    music_cells_boot_init();
    music_foreground_boot_init();
    g_gui_window_seen = 0U;
    assert(gui_read_window(&window));
    assert(!window.Published);

    gui_write_window_request(0U);
    (void)run_until_idle();
    assert(g_text_calls == 24U); /* 键表清零：全部重读。 */
}

/**
 * @brief 主机测试入口。
 * @return 0 表示全部通过。
 */
int main(void)
{
    test_without_any_request_nothing_happens();
    test_the_locator_miss_hook_only_changes_the_locator_handed_to_the_file_read();
    test_writing_a_request_wakes_the_foreground_but_publishing_a_result_does_not();
    test_a_window_is_published_only_when_all_rows_are_ready();
    test_the_last_window_holds_only_the_rows_that_exist();
    test_a_window_starting_beyond_the_list_is_published_with_no_rows();
    test_a_newer_window_request_overrides_the_older_one();
    test_rows_already_read_are_not_read_again();
    test_a_fully_cached_window_is_republished_without_any_file_call();
    test_title_and_window_share_the_row_cache();
    test_the_title_is_looked_at_before_the_window_in_every_step();
    test_the_title_result_echoes_the_request_sequence_and_only_the_latest_request_is_answered();
    test_title_generation_zero_means_current_and_a_mismatch_answers_zero();
    test_an_out_of_range_title_is_silent_for_current_and_warns_for_an_explicit_generation();
    test_a_title_request_that_is_not_wanted_reads_nothing();
    test_a_generation_change_during_a_fill_publishes_an_empty_window();
    test_after_the_empty_window_the_latest_request_is_served_again_without_a_new_request();
    test_the_empty_window_and_the_new_window_are_two_separate_publications();
    test_card_removal_publishes_one_empty_window_and_the_card_back_publishes_the_window_again();
    test_the_catalog_not_ready_yet_gets_an_empty_window_and_the_publish_gets_the_real_one();
    test_the_empty_window_notes_the_volume_edge_for_the_clear_latency();
    test_an_open_failure_shows_the_fallback_name_but_is_not_cached();
    test_a_read_error_is_not_cached();
    test_the_volume_going_unavailable_on_the_twelfth_row_publishes_nothing_and_keeps_the_window();
    test_a_volume_unavailable_row_in_the_middle_waits_and_is_not_cached();
    test_stable_results_are_cached();
    test_a_defensive_failure_leaves_the_row_blank_without_stopping_the_window();
    test_a_busy_open_is_retried_after_about_10_ms_and_never_shows_the_fallback_name();
    test_the_result_after_a_busy_retry_is_cached();
    test_a_busy_title_is_retried_and_only_the_good_answer_is_published();
    test_busy_is_retried_without_an_upper_bound_and_the_log_is_throttled();
    test_a_title_busy_is_also_retried_without_an_upper_bound();
    test_a_persistent_busy_ends_when_the_catalog_generation_changes();
    test_a_title_asked_before_the_catalog_was_ready_is_answered_again_when_it_is_published();
    test_a_title_read_with_the_volume_gone_answers_zero_without_a_warning();
    test_a_defensive_title_failure_warns();
    test_a_title_read_failure_shows_the_fallback_and_is_not_cached();
    test_a_request_that_could_not_be_read_because_of_the_lock_is_retried_soon();
    test_an_idle_step_waits_for_a_notification_and_a_busy_step_asks_to_be_called_again();
    test_boot_init_forgets_cached_rows_and_pending_work();
    puts("music_foreground: OK");
    return 0;
}
