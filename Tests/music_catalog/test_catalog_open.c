/**
 * @file test_catalog_open.c
 * @brief 曲库的主机测试：扫描单步、错误分类、发布规则与卷序号、一次读全、曲库短锁，以及按文件定位符打开
 *        （STOR-1、STOR-7、STOR-17、ARC-19、ARC-22、DIAG-4、DIAG-5、DIAG-6）。
 * @note  链接真实的 catalog.c、catalog_sheet.c、catalog_track_open.c、catalog_track_text.c 与
 *        catalog_track_diag.c；Filesystem Service、Log Service、DWT Adapter 与卷状态函数由本文件的假实现替换，
 *        只实现既有公开函数；曲库短锁走共用 FreeRTOS 桩。经音乐注册进注册表的后台入口（`music_background_step()`，它接曲库扫描单步 `catalog_scan_step()`）、一次读全、诊断出口观察行为；每一步最多一次文件调用由驱动函数 `step_once()` 对每一步断言。
 *        主机测试是单线程的：证明每一步的分支、返回值和发布结果，不证明真实任务切换、锁的真实时长与后台任务的 CPU 份额。
 */

#include "APP/apps/music/catalog/catalog.h"
#include "APP/apps/music/catalog/catalog_sheet.h"
#include "APP/apps/music/catalog/catalog_track_diag.h"
#include "APP/apps/music/catalog/catalog_track_text.h"
#include "APP/apps/music/music_background.h"
#include "APP/tasks/background/background_task_config.h"
#include "APP/tasks/volume/volume_event.h"

#include "freertos_stub.h"

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_text.h"
#include "Service/log/log_service.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- 常量核对（STOR-17、DIAG-4） ---- */
_Static_assert(CATALOG_MUSIC_MAX_NUM == 12000U, "catalog holds 12000 tracks (STOR-17)");
_Static_assert(CATALOG_MUSIC_MAX_NUM <= UINT16_MAX, "catalog index must fit uint16_t");
_Static_assert(CATALOG_MUSIC_POOL_SIZE == (4U * 1024U * 1024U), "path pool stays 4 MiB (#172 G)");
_Static_assert((CATALOG_MUSIC_POOL_SIZE / CATALOG_MUSIC_MAX_NUM) >= 300U,
               "pool leaves about 350 B of path per track");
_Static_assert(CATALOG_OPEN_BUDGET_US == 30000U, "single open budget is 30 ms (DIAG-4)");
_Static_assert(CATALOG_WINDOW_BUDGET_US == 500000U, "window budget is 0.5 s (DIAG-4)");
_Static_assert(CATALOG_OPEN_BUDGET_US < CATALOG_TRACK_BUDGET_US,
               "single open budget is tighter than the 1 s single-row log threshold");
_Static_assert(CATALOG_WINDOW_BUDGET_US < CATALOG_TRACK_BUDGET_US,
               "window budget is tighter than the 1 s single-row log threshold");

#define ROOT_ID_BASE 0U
#define SUB_ID_BASE  100000U
#define LOG_MAX      64U
#define LOG_TEXT_MAX 160U

/* ---- 假 DWT：1 MHz，一个周期即 1 µs ---- */
static uint32_t g_cycles;
static uint32_t g_foreground_wakes; /* foreground_task_wake 被调用的次数。 */
static bool catalog_lock_is_held(void);

/**
 * @brief 假周期计数器启动：恒成功。
 * @return 恒为 true。
 */
bool CortexM7CycleCounter_Start(void)
{
    return true;
}

/**
 * @brief 假周期计数器读数：由测试用 g_cycles 推进。
 * @return 当前周期数。
 */
uint32_t CortexM7CycleCounter_Read(void)
{
    return g_cycles;
}

/**
 * @brief 假核心频率：1 MHz，使一个周期等于 1 µs。
 * @return 1000000。
 */
uint32_t CortexM7CycleCounter_GetFrequencyHz(void)
{
    return 1000000U;
}

/**
 * @brief 前台任务叫醒函数的替身：计次。曲库发布后叫醒前台一次（扫描单步的发布成功路径），用来核对发布规则的这一句。
 */
void foreground_task_wake(void)
{
    g_foreground_wakes++;
}

/* ---- 假 Log：记录每一行 ---- */
typedef struct
{
    Service_Log_LevelTypeDef level;
    char tag[16];
    char text[LOG_TEXT_MAX];
} log_line_t;

static log_line_t g_log[LOG_MAX];
static uint32_t g_log_count;
static uint32_t g_log_cost_us; /* 一次日志投递消耗的微秒（假 DWT 推进），用来证明日志不计入打开耗时 */

/**
 * @brief 假日志投递：记录等级、标签与文本，并核对文本不超过 Service_Log 的单条容量。
 * @param[in] level 日志等级。
 * @param[in] tag 标签。
 * @param[in] text 文本。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Log_Post(Service_Log_LevelTypeDef level, const char *tag, const char *text)
{
    assert(!catalog_lock_is_held()); /* 持锁只拷内存：不投日志 */
    if (g_log_count < LOG_MAX)
    {
        g_log[g_log_count].level = level;
        snprintf(g_log[g_log_count].tag, sizeof(g_log[g_log_count].tag), "%s", tag);
        assert(strlen(text) < 128U); /* Service_Log 单条文本容量 */
        snprintf(g_log[g_log_count].text, sizeof(g_log[g_log_count].text), "%s", text);
        g_log_count++;
    }
    g_cycles += g_log_cost_us;
    return SERVICE_OK;
}

/* ---- 假卷状态与曲库锁观察 ---- */
static Volume_StatusTypeDef g_volume;     /* 假 SD 卷状态；测试直接改它来模拟换卡、拔卡 */
static uint32_t g_volume_status_cost_us;  /* 一次 volume_status 消耗的微秒（假 DWT）；曲库在锁内调它，用来造出可测的持锁时间 */
static SemaphoreHandle_t g_lock;          /* 曲库锁（catalog_boot_init 创建的那把） */

/**
 * @brief 假卷状态函数（真实的在 volume_event.c，读一个字、不拿锁）。
 * @param[in] volume 卷，须为 SD。
 * @return 测试设定的状态。
 */
Volume_StatusTypeDef volume_status(Volume_IdTypeDef volume)
{
    assert(volume == VOLUME_ID_SD);
    g_cycles += g_volume_status_cost_us;
    return g_volume;
}

/**
 * @brief 曲库锁此刻是否被持有（拿成功次数不等于放成功次数）。
 * @return true 被持有。
 */
static bool catalog_lock_is_held(void)
{
    const FreertosStub_SemaphoreStats stats = freertos_stub_semaphore_stats(g_lock);

    return stats.take_successes != stats.give_successes;
}

/**
 * @brief 统计以 prefix 开头的日志行数。
 * @param[in] prefix 行首文本。
 * @return 行数。
 */
static uint32_t log_count_prefix(const char *prefix)
{
    uint32_t n = 0U;

    for (uint32_t i = 0U; i < g_log_count; i++)
    {
        if (strncmp(g_log[i].text, prefix, strlen(prefix)) == 0)
        {
            n++;
        }
    }
    return n;
}

/**
 * @brief 返回以 prefix 开头的最后一行日志。
 * @param[in] prefix 行首文本。
 * @return 日志行；没有则中止。
 */
static const log_line_t *log_last_prefix(const char *prefix)
{
    for (uint32_t i = g_log_count; i > 0U; i--)
    {
        if (strncmp(g_log[i - 1U].text, prefix, strlen(prefix)) == 0)
        {
            return &g_log[i - 1U];
        }
    }
    assert(!"expected log line not found");
    return NULL;
}

/* ---- 假 Filesystem Service：合成曲库与按定位符打开 ---- */
static uint32_t g_root_count;      /* Music/ 下的文件数 */
static uint32_t g_sub_count;       /* Music/Sub/ 下的文件数 */
static uint32_t g_zero_locator_id; /* 该文件扫描时给全零定位符；UINT32_MAX 表示没有 */
static uint32_t g_stale_id;        /* 该文件按定位符打开时报核对不符（介质已变）；UINT32_MAX 表示没有 */
static bool g_locator_failed;      /* 按定位符打开一律报其他失败（BUSY） */
static Service_StatusTypeDef g_locator_failed_status = SERVICE_BUSY; /* 上面那个失败的具体状态 */
static uint32_t g_locator_cost_us; /* 一次按定位符打开消耗的微秒 */
static uint32_t g_path_cost_us;    /* 一次按路径打开消耗的微秒 */
static uint32_t g_locator_calls;
static uint32_t g_path_calls;
static uint32_t g_last_opened_id;
static uint32_t g_open_handles;
static uint32_t g_dir_token;
static uint32_t g_dir_pos;
static uint32_t g_dir_read_cost_us; /* 一次目录读取消耗的微秒（推进假 DWT），扫描耗时的来源 */
static uint32_t g_dir_calls_in_step; /* 当前这一步里目录接口（打开、读、关）被调用的次数；驱动函数断言一步最多一次 */
static bool g_unavailable_removes_card; /* 假目录接口返回“卷不可用”类状态时，同时让卷消失（模拟卡被拔走，卷任务已卸载） */
static bool g_read_dir_fail;        /* 目录读取一律报错 */
static Service_StatusTypeDef g_open_root_status; /* 非 SERVICE_OK 时，打开 Music/ 返回它（卷不可用等） */
static uint32_t g_open_root_status_times;        /* 上面那个状态返回几次之后恢复正常；0 表示一直返回 */
static uint32_t g_open_dir_calls;                /* 本次扫描里 OpenDirectory 被调用的次数 */
static Service_StatusTypeDef g_open_sub_status;  /* 非 SERVICE_OK 时，打开 Music/Sub 返回它 */
static uint32_t g_open_sub_status_times;         /* 同 g_open_root_status_times */
static Service_StatusTypeDef g_read_dir_status;  /* 非 SERVICE_OK 时，读满 g_read_dir_status_after 条之后读目录返回它 */
static uint32_t g_read_dir_status_after;
static uint32_t g_dir_reads;                     /* 本次扫描里 ReadDirectory 被调用的次数 */
static bool g_music_missing;        /* 没有 Music/ 目录 */
static uint32_t g_open_root_busy;   /* 打开 Music/ 先报这么多次 SERVICE_BUSY（槽用尽） */
static uint32_t g_read_busy;        /* 读目录先报这么多次 SERVICE_BUSY */
static uint32_t g_close_busy;       /* 关目录先报这么多次 SERVICE_BUSY */
static Service_StatusTypeDef g_close_status;  /* 非 SERVICE_OK 时，关目录返回它 */
static uint32_t g_close_calls;      /* CloseDirectory 被调用的次数（含失败） */
static uint32_t g_swap_after_reads; /* 非 0：第 N 次读目录时卷序号加 2（换卡） */
static bool g_swap_on_close;        /* 关目录时卷序号加 2（扫描做完、发布之前换卡） */
static bool g_busy_swaps_volume;    /* 报 SERVICE_BUSY 的同时卷序号加 2 */
static void (*g_read_hook)(void);   /* 每次读目录开头调用，用来在扫描途中以读者身份调用曲库 */

/* 目录树假实现：非空时 OpenDirectory/ReadDirectory 按它走（路径对不上返回 SERVICE_ERROR），不用合成曲库的两层结构。 */
typedef struct
{
    const char *path;
    const char *entries[12]; /* NULL 结尾；以 '/' 结尾的是子目录，其余是文件 */
} TreeDir;
static const TreeDir *g_tree;
static uint32_t g_tree_count;

static uint32_t g_scans_started;    /* 自 world_reset 起 `scan()` 已启动的扫描次数；第二次起 `scan()` 先换卡（卷序号加 2）才会重扫 */

static uint8_t g_file_bytes[512];
static uint32_t g_file_length;

/**
 * @brief 生成文件 id 对应的定位符。
 * @param[in] id 文件 id。
 * @return 定位符。
 */
static Service_Filesystem_FileLocatorTypeDef locator_for(uint32_t id)
{
    Service_Filesystem_FileLocatorTypeDef locator = {{1000U + id, id * 32U, 5000U + id, 4096U + id}};

    return locator;
}

/**
 * @brief 判断定位符是否等于 locator_for(id)。
 * @param[in] a 定位符。
 * @param[in] id 文件 id。
 * @return true 相等。
 */
static bool locator_is(const Service_Filesystem_FileLocatorTypeDef *a, uint32_t id)
{
    const Service_Filesystem_FileLocatorTypeDef b = locator_for(id);

    return memcmp(a, &b, sizeof(b)) == 0;
}

/**
 * @brief 把路径解析成文件 id。
 * @param[in] path 曲库相对路径。
 * @param[out] id 文件 id。
 * @return true 路径属于合成曲库且文件存在。
 */
static bool path_to_id(const char *path, uint32_t *id)
{
    unsigned n;
    char tail;

    if ((sscanf(path, "Music/t%5u.mp3%c", &n, &tail) == 1) && (n < g_root_count))
    {
        *id = ROOT_ID_BASE + n;
        return true;
    }
    if ((sscanf(path, "Music/Sub/s%3u.mp3%c", &n, &tail) == 1) && (n < g_sub_count))
    {
        *id = SUB_ID_BASE + n;
        return true;
    }
    return false;
}

/**
 * @brief 判断文件 id 是否存在。
 * @param[in] id 文件 id。
 * @return true 存在。
 */
static bool id_exists(uint32_t id)
{
    if (id >= SUB_ID_BASE)
    {
        return (id - SUB_ID_BASE) < g_sub_count;
    }
    return id < g_root_count;
}

/**
 * @brief 取脚本里的下一个状态：状态为 OK 时返回 OK；次数为 0 表示一直返回该状态；否则返回该状态 `*times` 次，用完恢复 OK。
 * @param[in,out] status 脚本状态，次数用完后清成 SERVICE_OK。
 * @param[in,out] times 剩余次数。
 * @return 这一次该返回的状态。
 */
static Service_StatusTypeDef take_scripted_status(Service_StatusTypeDef *status, uint32_t *times)
{
    const Service_StatusTypeDef current = *status;

    if ((current != SERVICE_OK) && (*times != 0U))
    {
        (*times)--;
        if (*times == 0U)
        {
            *status = SERVICE_OK;
        }
    }
    return current;
}

/**
 * @brief 假目录接口要返回 status 之前调用：状态是“卷不可用”类且打开了“卡被拔走”开关时，让假卷消失（序号加 1、未挂载）。
 * @param[in] status 即将返回的状态。
 * @return 原样返回 status，方便写成 `return note_status(x);`。
 */
static Service_StatusTypeDef note_status(Service_StatusTypeDef status)
{
    if (g_unavailable_removes_card && Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        g_volume.Mounted = false;
        g_volume.Sequence += 1U;
    }
    return status;
}

/**
 * @brief 假目录打开：只认 `Music` 与（有子文件夹时）`Music/Sub`。
 * @param[in] volume 卷，须为 SD。
 * @param[in] path 目录路径。
 * @param[out] directory 目录句柄。
 * @return SERVICE_OK 或 SERVICE_ERROR。
 */
Service_StatusTypeDef Service_Filesystem_OpenDirectory(Service_Filesystem_VolumeTypeDef volume,
                                                       const char *path,
                                                       Service_Filesystem_DirectoryHandleTypeDef *directory)
{
    assert(volume == SERVICE_FILESYSTEM_VOLUME_SD);
    assert(!catalog_lock_is_held()); /* 持锁只拷内存：不调文件接口 */
    g_dir_calls_in_step++;
    if (g_tree != NULL)
    {
        for (uint32_t i = 0U; i < g_tree_count; i++)
        {
            if (strcmp(g_tree[i].path, path) == 0)
            {
                g_open_dir_calls++;
                g_dir_token = 100U + i;
                g_dir_pos = 0U;
                directory->Token = g_dir_token;
                return SERVICE_OK;
            }
        }
        return SERVICE_ERROR;
    }
    if (g_music_missing)
    {
        return SERVICE_ERROR;
    }
    g_open_dir_calls++;
    if ((strcmp(path, "Music") == 0) && (g_open_root_busy != 0U))
    {
        g_open_root_busy--;
        if (g_busy_swaps_volume)
        {
            g_volume.Sequence += 2U;
        }
        return SERVICE_BUSY;
    }
    if (strcmp(path, "Music") == 0)
    {
        const Service_StatusTypeDef scripted = take_scripted_status(&g_open_root_status, &g_open_root_status_times);

        if (scripted != SERVICE_OK)
        {
            return note_status(scripted);
        }
        g_dir_token = 1U;
    }
    else if ((strcmp(path, "Music/Sub") == 0) && (g_sub_count != 0U))
    {
        const Service_StatusTypeDef scripted = take_scripted_status(&g_open_sub_status, &g_open_sub_status_times);

        if (scripted != SERVICE_OK)
        {
            return note_status(scripted);
        }
        g_dir_token = 2U;
    }
    else
    {
        return SERVICE_ERROR;
    }
    g_dir_pos = 0U;
    directory->Token = g_dir_token;
    return SERVICE_OK;
}

/**
 * @brief 假目录读取：先给本目录的文件，根目录再给一个 `Sub` 子目录，最后给空名结束。
 * @param[in] directory 目录句柄。
 * @param[out] entry 目录条目；文件条目带 locator_for(id) 或（指定的）全零定位符。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Filesystem_ReadDirectory(Service_Filesystem_DirectoryHandleTypeDef directory,
                                                       Service_Filesystem_DirectoryEntryTypeDef *entry)
{
    uint32_t files = (directory.Token == 1U) ? g_root_count : g_sub_count;
    uint32_t base = (directory.Token == 1U) ? ROOT_ID_BASE : SUB_ID_BASE;

    assert(!catalog_lock_is_held());
    g_dir_calls_in_step++;
    g_cycles += g_dir_read_cost_us;
    g_dir_reads++;
    if (directory.Token >= 100U)
    {
        const TreeDir *dir = &g_tree[directory.Token - 100U];
        const char *name = dir->entries[g_dir_pos];

        if ((g_read_dir_status != SERVICE_OK) && (g_dir_reads > g_read_dir_status_after))
        {
            return note_status(g_read_dir_status);
        }

        memset(entry, 0, sizeof(*entry));
        if (name != NULL)
        {
            const size_t length = strlen(name);

            g_dir_pos++;
            if (name[length - 1U] == '/')
            {
                memcpy(entry->Name, name, length - 1U);
                entry->IsDirectory = true;
            }
            else
            {
                memcpy(entry->Name, name, length);
                entry->Locator = locator_for(1U);
            }
        }
        return SERVICE_OK;
    }
    if (g_read_hook != NULL)
    {
        g_read_hook();
    }
    if ((g_swap_after_reads != 0U) && (g_dir_reads == g_swap_after_reads))
    {
        g_volume.Sequence += 2U;
    }
    if (g_read_busy != 0U)
    {
        g_read_busy--;
        return SERVICE_BUSY;
    }
    if ((g_read_dir_status != SERVICE_OK) && (g_dir_reads > g_read_dir_status_after))
    {
        return note_status(g_read_dir_status);
    }
    if (g_read_dir_fail)
    {
        return SERVICE_ERROR;
    }

    memset(entry, 0, sizeof(*entry));
    if (g_dir_pos < files)
    {
        uint32_t id = base + g_dir_pos;

        if (directory.Token == 1U)
        {
            snprintf(entry->Name, sizeof(entry->Name), "t%05u.mp3", (unsigned)g_dir_pos);
        }
        else
        {
            snprintf(entry->Name, sizeof(entry->Name), "s%03u.mp3", (unsigned)g_dir_pos);
        }
        entry->Size = g_file_length;
        if (id != g_zero_locator_id)
        {
            entry->Locator = locator_for(id);
        }
        g_dir_pos++;
    }
    else if ((directory.Token == 1U) && (g_sub_count != 0U) && (g_dir_pos == files))
    {
        snprintf(entry->Name, sizeof(entry->Name), "Sub");
        entry->IsDirectory = true;
        g_dir_pos++;
    }
    return SERVICE_OK;
}

/**
 * @brief 假目录关闭。
 * @param[in] directory 目录句柄。
 * @return 默认 SERVICE_OK；可注入 SERVICE_BUSY 或其他失败。
 */
Service_StatusTypeDef Service_Filesystem_CloseDirectory(Service_Filesystem_DirectoryHandleTypeDef directory)
{
    (void)directory;
    assert(!catalog_lock_is_held());
    g_dir_calls_in_step++;
    g_close_calls++;
    if (g_swap_on_close)
    {
        g_volume.Sequence += 2U;
    }
    if (g_close_busy != 0U)
    {
        g_close_busy--;
        return SERVICE_BUSY;
    }
    return note_status(g_close_status);
}

/**
 * @brief 假按路径打开：记录调用并推进假 DWT。
 * @param[in] volume 卷，须为 SD。
 * @param[in] path 曲库相对路径。
 * @param[in] mode 打开方式，须为只读。
 * @param[out] file 文件句柄。
 * @return SERVICE_OK；路径不属于合成曲库时为 SERVICE_ERROR。
 */
Service_StatusTypeDef Service_Filesystem_OpenFile(Service_Filesystem_VolumeTypeDef volume,
                                                  const char *path,
                                                  Service_Filesystem_FileModeTypeDef mode,
                                                  Service_Filesystem_FileHandleTypeDef *file)
{
    uint32_t id;

    assert(volume == SERVICE_FILESYSTEM_VOLUME_SD);
    assert(mode == SERVICE_FILESYSTEM_FILE_MODE_READ);
    g_path_calls++;
    g_cycles += g_path_cost_us;
    if (!path_to_id(path, &id))
    {
        return SERVICE_ERROR;
    }
    g_last_opened_id = id;
    g_open_handles++;
    file->Token = 1U;
    return SERVICE_OK;
}

/**
 * @brief 假按定位符打开：复现三态约定，定位符与 locator_for(id) 一致且介质未变时 OPENED，否则 MISMATCH；可注入 FAILED。
 * @param[in] volume 卷，须为 SD。
 * @param[in] locator 文件定位符。
 * @param[out] file 文件句柄。
 * @param[out] status 可为空；FAILED 时为 SERVICE_BUSY。
 * @return OPENED、MISMATCH 或 FAILED。
 */
Service_Filesystem_LocatorOpenResultTypeDef Service_Filesystem_OpenFileByLocator(
    Service_Filesystem_VolumeTypeDef volume,
    const Service_Filesystem_FileLocatorTypeDef *locator,
    Service_Filesystem_FileHandleTypeDef *file,
    Service_StatusTypeDef *status)
{
    uint32_t id;
    Service_StatusTypeDef local;

    assert(volume == SERVICE_FILESYSTEM_VOLUME_SD);
    if (status == NULL)
    {
        status = &local;
    }
    *status = SERVICE_OK;
    g_locator_calls++;
    g_cycles += g_locator_cost_us;
    if (g_locator_failed)
    {
        *status = g_locator_failed_status;
        return SERVICE_FILESYSTEM_LOCATOR_FAILED;
    }
    id = (locator->Opaque[0] >= 1000U) ? (locator->Opaque[0] - 1000U) : UINT32_MAX;
    if ((id == UINT32_MAX) || !id_exists(id) || !locator_is(locator, id) || (id == g_stale_id))
    {
        return SERVICE_FILESYSTEM_LOCATOR_MISMATCH;
    }
    g_last_opened_id = id;
    g_open_handles++;
    file->Token = 1U;
    return SERVICE_FILESYSTEM_LOCATOR_OPENED;
}

/**
 * @brief 假 seek：恒成功（读总是从文件头给）。
 * @param[in] file 文件句柄。
 * @param[in] offset 偏移。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Filesystem_SeekFile(Service_Filesystem_FileHandleTypeDef file, uint64_t offset)
{
    (void)file;
    (void)offset;
    return SERVICE_OK;
}

/**
 * @brief 假读取：从文件头拷贝至多 requested 字节。
 * @param[in] file 文件句柄。
 * @param[out] buffer 输出缓冲。
 * @param[in] requested 请求字节数。
 * @param[out] transferred 实际字节数。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Filesystem_ReadFile(Service_Filesystem_FileHandleTypeDef file,
                                                  void *buffer,
                                                  uint32_t requested,
                                                  uint32_t *transferred)
{
    (void)file;
    /* 每次读都从当前 seek 位置开始会更真实；曲目文本只在首块读到 TIT2，故从文件头给即可。 */
    uint32_t n = (requested < g_file_length) ? requested : g_file_length;

    memcpy(buffer, g_file_bytes, n);
    *transferred = n;
    return SERVICE_OK;
}

/**
 * @brief 假取文件长度。
 * @param[in] file 文件句柄。
 * @param[out] size 文件长度。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Filesystem_GetFileSize(Service_Filesystem_FileHandleTypeDef file, uint64_t *size)
{
    (void)file;
    *size = g_file_length;
    return SERVICE_OK;
}

/**
 * @brief 假关闭：核对没有重复关闭，供用例末检查句柄泄漏。
 * @param[in] file 文件句柄。
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Filesystem_CloseFile(Service_Filesystem_FileHandleTypeDef file)
{
    (void)file;
    assert(g_open_handles > 0U);
    g_open_handles--;
    return SERVICE_OK;
}

/**
 * @brief 假 GBK 查表：恒无映射（用例的标题是 ASCII）。
 * @param[in] gbk_code GBK 双字节码。
 * @return 恒为 0。
 */
uint16_t Service_Filesystem_GbkToUnicode(uint16_t gbk_code)
{
    (void)gbk_code;
    return 0U;
}

/* ---- 测试夹具 ---- */

/** @brief 构造带 TIT2 "Fake Title" 的 ID3v2.4 假文件。 */
static void build_file_bytes(void)
{
    static const char title[] = "Fake Title";
    const uint32_t frame_payload = 1U + (uint32_t)(sizeof(title) - 1U);
    const uint32_t tag_body = 10U + frame_payload + 64U; /* 帧头 + 负载 + 填充 */
    uint32_t n = 0U;

    memset(g_file_bytes, 0, sizeof(g_file_bytes));
    memcpy(g_file_bytes, "ID3", 3U);
    g_file_bytes[3] = 4U;
    g_file_bytes[6] = (uint8_t)((tag_body >> 21) & 0x7FU);
    g_file_bytes[7] = (uint8_t)((tag_body >> 14) & 0x7FU);
    g_file_bytes[8] = (uint8_t)((tag_body >> 7) & 0x7FU);
    g_file_bytes[9] = (uint8_t)(tag_body & 0x7FU);
    n = 10U;
    memcpy(g_file_bytes + n, "TIT2", 4U);
    g_file_bytes[n + 4U] = 0U;
    g_file_bytes[n + 5U] = 0U;
    g_file_bytes[n + 6U] = 0U;
    g_file_bytes[n + 7U] = (uint8_t)frame_payload;
    n += 10U;
    g_file_bytes[n++] = 3U; /* UTF-8 */
    memcpy(g_file_bytes + n, title, sizeof(title) - 1U);
    n += (uint32_t)(sizeof(title) - 1U);
    n += 64U;
    for (uint32_t i = 0U; i < 128U; i++)
    {
        g_file_bytes[n + i] = (uint8_t)((i & 1U) ? 0xFBU : 0xFFU);
    }
    g_file_length = n + 128U;
}

/** @brief 复位所有假状态；每个用例开头调用。 */
static void world_reset(void)
{
    g_root_count = 0U;
    g_sub_count = 0U;
    g_zero_locator_id = UINT32_MAX;
    g_stale_id = UINT32_MAX;
    g_locator_failed = false;
    g_locator_failed_status = SERVICE_BUSY;
    g_locator_cost_us = 0U;
    g_path_cost_us = 0U;
    g_log_cost_us = 0U;
    g_locator_calls = 0U;
    g_path_calls = 0U;
    g_last_opened_id = UINT32_MAX;
    g_open_handles = 0U;
    g_dir_read_cost_us = 0U;
    g_dir_calls_in_step = 0U;
    g_tree = NULL;
    g_tree_count = 0U;
    g_unavailable_removes_card = false;
    g_scans_started = 0U;
    g_read_dir_fail = false;
    g_open_root_status = SERVICE_OK;
    g_open_root_status_times = 0U;
    g_open_dir_calls = 0U;
    g_open_sub_status = SERVICE_OK;
    g_open_sub_status_times = 0U;
    g_read_dir_status = SERVICE_OK;
    g_read_dir_status_after = 0U;
    g_dir_reads = 0U;
    g_music_missing = false;
    g_open_root_busy = 0U;
    g_read_busy = 0U;
    g_close_busy = 0U;
    g_close_status = SERVICE_OK;
    g_close_calls = 0U;
    g_swap_after_reads = 0U;
    g_swap_on_close = false;
    g_busy_swaps_volume = false;
    g_read_hook = NULL;
    g_volume_status_cost_us = 0U;
    g_volume.Mounted = true;
    g_volume.Sequence = 7U;
    freertos_stub_reset();
    catalog_boot_init();
    g_lock = freertos_stub_last_created_semaphore();
    g_cycles = 1000U;
    g_log_count = 0U;
    build_file_bytes();
}

/**
 * @brief 跑扫描作业一步：断言一步最多一次文件调用，把假时间推进它要求的等待。
 * @return 这一步的返回值：毫秒后再叫它，0 马上，`BACKGROUND_WAIT_FOREVER` 等通知。
 */
static uint32_t step_once(void)
{
    uint32_t wait;

    g_dir_calls_in_step = 0U;
    wait = music_background_step(); /* 音乐注册进注册表的 background_step，它接曲库扫描单步 */
    assert(g_dir_calls_in_step <= 1U); /* ARC-22：一步最多一次可能耗时的文件调用 */
    if ((wait != 0U) && (wait != BACKGROUND_WAIT_FOREVER))
    {
        g_cycles += wait * 1000U; /* 假 DWT 为 1 MHz：等待的毫秒也是真实经过的时间 */
        freertos_stub_set_tick(xTaskGetTickCount() + (TickType_t)wait);
    }
    return wait;
}

/**
 * @brief 一直跑到作业要求等通知（扫描做完、被放弃，或根本没有活）；有上限，防止状态机停不下来。
 * @return 跑过的步数。
 */
static uint32_t run_to_idle(void)
{
    for (uint32_t steps = 1U; steps <= 200000U; steps++)
    {
        if (step_once() == BACKGROUND_WAIT_FOREVER)
        {
            return steps;
        }
    }
    assert(!"the scan did not settle");
    return 0U;
}

/**
 * @brief 跑到日志里出现以 prefix 开头的行为止（每步之后看一次）；有上限。
 * @param[in] prefix 行首文本。
 * @return 出现时已跑过的步数。
 */
static uint32_t run_until_log(const char *prefix)
{
    for (uint32_t steps = 1U; steps <= 200000U; steps++)
    {
        (void)step_once();
        if (log_count_prefix(prefix) != 0U)
        {
            return steps;
        }
    }
    assert(!"expected log line did not appear");
    return 0U;
}

/**
 * @brief 扫描合成曲库：卷（重新）挂载，作业一路跑到做完。第二次起先换卡（卷序号加 2）才会重扫，
 *        因为扫描只在卷序号变化后发生（卷事件）。
 * @param[in] root Music/ 下文件数。
 * @param[in] sub Music/Sub/ 下文件数。
 */
static void scan(uint32_t root, uint32_t sub)
{
    g_root_count = root;
    g_sub_count = sub;
    g_volume.Mounted = true;
    if (g_scans_started++ != 0U)
    {
        g_volume.Sequence += 2U;
    }
    (void)run_to_idle();
}

/**
 * @brief 卡被拔走：卷消失（序号加 1，未挂载），作业跑到不再有活。
 */
static void unmount_card(void)
{
    g_volume.Mounted = false;
    g_volume.Sequence += 1U;
    (void)run_to_idle();
}

/**
 * @brief 当前已发布的曲库条数（代次为 0 时为 0）。
 * @return 条数。
 */
static uint16_t count_now(void)
{
    Catalog_TrackTypeDef track;

    (void)catalog_read_track(0U, &track);
    return track.Count;
}

/**
 * @brief 取指定位置的文件定位符。
 * @param[in] position 播放列表位置。
 * @param[out] locator 定位符。
 * @return true 位置有效；false 曲库不可用或位置越界。
 */
static bool locator_at(uint16_t position, Service_Filesystem_FileLocatorTypeDef *locator)
{
    Catalog_TrackTypeDef track;
    const bool found = catalog_read_track(position, &track);

    *locator = track.Locator;
    return found;
}

/**
 * @brief 读指定位置的曲目文本：先一次读全，再按它的路径和定位符读标签。
 * @param[in] position 播放列表位置。
 * @return 读取结果；位置无效（曲库不可用或越界）或卷不可用时为 NULL。
 */
static const CatalogTrackText_ResultTypeDef *read_text(uint16_t position)
{
    static Catalog_TrackTypeDef track;

    if (!catalog_read_track(position, &track))
    {
        return NULL;
    }
    return catalog_track_text_read(position, &track);
}

/**
 * @brief 找出路径等于 path 的播放列表位置。
 * @param[in] path 曲库相对路径。
 * @return 位置；没有则中止。
 */
static uint16_t catalog_index_of(const char *path)
{
    Catalog_TrackTypeDef track;

    for (uint16_t i = 0U; i < count_now(); i++)
    {
        assert(catalog_read_track(i, &track));
        if (strcmp(track.Path, path) == 0)
        {
            return i;
        }
    }
    assert(!"path not in catalog");
    return UINT16_MAX;
}

/** @brief 曲库上限 12000：超出的条目被截断，已收录部分的定位符完好。 */
static void test_catalog_limit_12000(void)
{
    Service_Filesystem_FileLocatorTypeDef locator;

    world_reset();
    scan(12500U, 0U);

    assert(count_now() == 12000U);
    assert(locator_at(0U, &locator));
    assert(locator_is(&locator, 0U));
    assert(locator_at(11999U, &locator));
    assert(locator_is(&locator, 11999U));
    assert(!locator_at(12000U, &locator));
    assert(log_count_prefix("Music Catalog Full.") == 1U);
}

/** @brief 扫描时每条随附文件定位符，子文件夹里的也一样。 */
static void test_scan_attaches_locator_to_each_entry(void)
{
    Service_Filesystem_FileLocatorTypeDef locator;
    char path[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

    world_reset();
    scan(3U, 2U);

    assert(count_now() == 5U);
    for (unsigned i = 0U; i < 3U; i++)
    {
        snprintf(path, sizeof(path), "Music/t%05u.mp3", i);
        assert(locator_at(catalog_index_of(path), &locator));
        assert(locator_is(&locator, ROOT_ID_BASE + i));
    }
    for (unsigned i = 0U; i < 2U; i++)
    {
        snprintf(path, sizeof(path), "Music/Sub/s%03u.mp3", i);
        assert(locator_at(catalog_index_of(path), &locator));
        assert(locator_is(&locator, SUB_ID_BASE + i));
    }
}

/** @brief 定位符只在同一曲库代次内有效：作废后取不到，重扫后换成新的。 */
static void test_locator_is_valid_only_within_one_generation(void)
{
    Service_Filesystem_FileLocatorTypeDef locator;
    uint32_t first_generation;

    world_reset();
    scan(4U, 0U);
    first_generation = catalog_generation();
    assert(locator_at(3U, &locator));

    unmount_card(); /* 卷消失：旧代次作废，定位符取不到 */
    assert(!locator_at(3U, &locator));

    scan(2U, 0U);
    assert(catalog_generation() != first_generation);
    assert(!locator_at(3U, &locator)); /* 新代次只有 2 条 */
    assert(locator_at(1U, &locator));
}

/** @brief 读曲目文本按定位符打开：不走路径，打开耗时单独给出。 */
static void test_text_read_opens_by_locator(void)
{
    const CatalogTrackText_ResultTypeDef *track;

    world_reset();
    scan(8U, 0U);
    g_locator_cost_us = 1234U;
    g_path_cost_us = 99999U;

    track = read_text(5U);

    assert(track != NULL);
    assert(strcmp(track->Text.Title, "Fake Title") == 0);
    assert(!track->OpenFailed);
    assert(track->OpenUs == 1234U);
    assert(g_locator_calls == 1U);
    assert(g_path_calls == 0U);
    assert(g_last_opened_id == 5U);
    assert(g_open_handles == 0U);
    assert(log_count_prefix("Locator miss") == 0U);
}

/** @brief 核对失败（介质上的目录项已变）退回按路径打开，记 WARN 并带播放列表位置。 */
static void test_locator_mismatch_falls_back_to_path_open(void)
{
    const CatalogTrackText_ResultTypeDef *track;
    const log_line_t *line;

    world_reset();
    scan(8U, 0U);
    g_stale_id = 5U;
    g_locator_cost_us = 1000U;
    g_path_cost_us = 2000U;
    g_log_cost_us = 700U; /* Locator miss 的日志投递耗时不属于打开 */

    track = read_text(5U);

    assert(track != NULL);
    assert(strcmp(track->Text.Title, "Fake Title") == 0);
    assert(!track->OpenFailed);
    assert(g_locator_calls == 1U);
    assert(g_path_calls == 1U);
    assert(g_last_opened_id == 5U); /* 退回路径打开的仍是同一首 */
    assert(track->OpenUs == 3000U); /* 两次尝试都计入打开耗时，但不含 Locator miss 日志投递的 700 µs */
    assert(g_open_handles == 0U);
    assert(log_count_prefix("Locator miss") == 1U);
    line = log_last_prefix("Locator miss");
    assert(line->level == SERVICE_LOG_LEVEL_WARN);
    assert(strcmp(line->text, "Locator miss pos=5") == 0);
}

/** @brief 扫描时没取到定位符（全零）与核对不符同样处理。 */
static void test_missing_locator_falls_back_to_path_open(void)
{
    const CatalogTrackText_ResultTypeDef *track;

    world_reset();
    g_zero_locator_id = 2U;
    scan(4U, 0U);

    track = read_text(2U);

    assert(track != NULL);
    assert(strcmp(track->Text.Title, "Fake Title") == 0);
    assert(g_path_calls == 1U);
    assert(g_last_opened_id == 2U);
    assert(strcmp(log_last_prefix("Locator miss")->text, "Locator miss pos=2") == 0);
}

/** @brief 每次核对失败各记一行，不合并。 */
static void test_each_locator_miss_is_logged(void)
{
    world_reset();
    scan(8U, 0U);
    g_stale_id = 1U;

    (void)read_text(1U);
    (void)read_text(1U);
    (void)read_text(3U); /* 3 号核对通过，不记 */

    assert(log_count_prefix("Locator miss") == 2U);
    assert(strcmp(log_last_prefix("Locator miss")->text, "Locator miss pos=1") == 0);
}

/** @brief 其他失败（槽用尽等）不是核对不符：不退回路径打开，也不记 Locator miss。 */
static void test_locator_open_failure_is_not_retried_by_path(void)
{
    const CatalogTrackText_ResultTypeDef *track;

    world_reset();
    scan(4U, 0U);
    g_locator_failed = true;

    track = read_text(1U);

    assert(track != NULL);
    assert(track->OpenFailed);
    assert(strcmp(track->Text.Title, "t00001") == 0); /* 回退文件主名 */
    assert(g_path_calls == 0U);
    assert(g_open_handles == 0U);
    assert(log_count_prefix("Locator miss") == 0U);
}

/**
 * @brief 打开时得到 NO_MEDIA / NOT_READY / INVALID_HANDLE（卷没了或正在对账）不是文件错误：不回退文件主名、
 *        不记为“打开失败”，不退回路径打开；返回空，行留空，随后的卷事件会把整个窗口收掉（#195 第 9 条）。
 */
static void test_text_read_treats_a_volume_unavailable_open_as_no_result(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (size_t index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        world_reset();
        scan(4U, 0U);
        g_locator_failed = true;
        g_locator_failed_status = unavailable[index];

        assert(read_text(1U) == NULL);
        assert(g_path_calls == 0U);
        assert(g_open_handles == 0U);
        assert(log_count_prefix("Locator miss") == 0U);
    }
}

/** @brief 曲库下标越界时无路径可开：返回空，不打开任何文件。 */
static void test_text_read_with_unknown_catalog_index_returns_null(void)
{
    world_reset();
    scan(2U, 0U);

    assert(read_text(5U) == NULL);
    assert(g_locator_calls == 0U);
    assert(g_path_calls == 0U);
}

/**
 * @brief 以给定耗时模拟一次窗口行读取并送入诊断。
 * @param[in] open_us 打开耗时。
 * @param[in] total_us 整行耗时（含打开）。
 * @param[in] open_failed 是否打开失败。
 */
static void window_row(uint32_t open_us, uint32_t total_us, bool open_failed)
{
    CatalogTrackText_ResultTypeDef track;
    const uint32_t start = catalog_track_diag_begin();

    memset(&track, 0, sizeof(track));
    track.OpenUs = open_us;
    track.OpenFailed = open_failed;
    track.Text.Result = CATALOG_TEXT_TAGGED;
    track.Text.Reads = 1U;
    g_cycles += total_us;
    catalog_track_diag_row_done(start, &track);
}

/** @brief 曲名 INFO 带 open=，标签读取耗时 us= 不含打开。 */
static void test_name_info_separates_open_from_tag_read(void)
{
    CatalogTrackText_ResultTypeDef track;
    const log_line_t *line;
    uint32_t start;

    world_reset();
    memset(&track, 0, sizeof(track));
    track.OpenUs = 1234U;
    track.Text.Result = CATALOG_TEXT_TAGGED;
    track.Text.Reads = 2U;
    start = catalog_track_diag_begin();
    g_cycles += 5000U;

    catalog_track_diag_name_done(7U, start, &track);

    line = log_last_prefix("Tag idx=");
    assert(line->level == SERVICE_LOG_LEVEL_INFO);
    assert(strcmp(line->text, "Tag idx=7 src=title reads=2 open=1234 us=3766") == 0);
}

/** @brief 回退行同样带 open=。 */
static void test_name_fallback_info_has_open(void)
{
    CatalogTrackText_ResultTypeDef track;
    uint32_t start;

    world_reset();
    memset(&track, 0, sizeof(track));
    track.OpenUs = 800U;
    track.Text.Result = CATALOG_TEXT_NO_ID3V2;
    track.Text.Reads = 1U;
    start = catalog_track_diag_begin();
    g_cycles += 1000U;

    catalog_track_diag_name_done(3U, start, &track);

    assert(strcmp(log_last_prefix("Tag idx=")->text, "Tag idx=3 src=file why=no_id3v2 reads=1 open=800 us=200") == 0);
}

/** @brief 窗口汇总：单行打开恰为 30 ms、整窗恰为 0.5 s 都不触发。 */
static void test_window_summary_silent_at_the_budgets(void)
{
    world_reset();
    catalog_track_diag_window_begin();
    for (unsigned i = 0U; i < 12U; i++)
    {
        window_row(CATALOG_OPEN_BUDGET_US, 41666U, false);
    }
    catalog_track_diag_window_end(); /* 12 × 41666 = 499992 µs */

    assert(log_count_prefix("Tag win") == 0U);
}

/** @brief 窗口汇总：单行打开超过 30 ms 触发，并给出最长单行打开。 */
static void test_window_summary_fires_on_slow_single_open(void)
{
    const log_line_t *line;

    world_reset();
    catalog_track_diag_window_begin();
    for (unsigned i = 0U; i < 11U; i++)
    {
        window_row(2000U, 5000U, false);
    }
    window_row(CATALOG_OPEN_BUDGET_US + 1U, 40000U, false);
    catalog_track_diag_window_end();

    assert(log_count_prefix("Tag win") == 1U);
    line = log_last_prefix("Tag win");
    assert(line->level == SERVICE_LOG_LEVEL_INFO);
    assert(strstr(line->text, "omax=30001") != NULL);
}

/** @brief 窗口汇总：整窗超过 0.5 s 触发，即使每行打开都很快。 */
static void test_window_summary_fires_on_slow_window(void)
{
    world_reset();
    catalog_track_diag_window_begin();
    for (unsigned i = 0U; i < 12U; i++)
    {
        window_row(5000U, 41667U, false);
    }
    catalog_track_diag_window_end(); /* 12 × 41667 = 500004 µs */

    assert(log_count_prefix("Tag win") == 1U);
    assert(log_last_prefix("Tag win")->level == SERVICE_LOG_LEVEL_INFO);
}

/** @brief 窗口汇总：WARN 级标签原因照旧触发，级别为 WARN。 */
static void test_window_summary_still_fires_on_warn_reason(void)
{
    world_reset();
    catalog_track_diag_window_begin();
    window_row(1000U, 2000U, false);
    window_row(0U, 500U, true);
    catalog_track_diag_window_end();

    assert(log_count_prefix("Tag win") == 1U);
    assert(log_last_prefix("Tag win")->level == SERVICE_LOG_LEVEL_WARN);
}

/** @brief 窗口汇总：1 s 是单行日志阈值（此处用单行总耗时超限，打开很快）。 */
static void test_window_summary_defensive_threshold_is_kept(void)
{
    world_reset();
    catalog_track_diag_window_begin();
    window_row(1000U, CATALOG_TRACK_BUDGET_US + 1U, false);
    catalog_track_diag_window_end();

    assert(log_count_prefix("Tag win") == 1U);
}

/** @brief 汇总行最长的现实取值也放得进 Service_Log 的 128 B。 */
static void test_window_summary_fits_log_capacity(void)
{
    world_reset();
    catalog_track_diag_window_begin();
    for (unsigned i = 0U; i < 12U; i++)
    {
        window_row(250000U, 300000U, true);
    }
    catalog_track_diag_window_end();

    assert(strstr(log_last_prefix("Tag win")->text, "rerr=") != NULL);
}

/** @brief Stream INFO：位置、请求的偏移、打开与 lseek 耗时。 */
static void test_stream_info_line(void)
{
    const log_line_t *line;

    world_reset();
    catalog_track_diag_stream_done(12U, 345678U, 2100U, 870U);

    line = log_last_prefix("Stream");
    assert(line->level == SERVICE_LOG_LEVEL_INFO);
    assert(strcmp(line->tag, "STORAGE") == 0);
    assert(strcmp(line->text, "Stream pos=12 off=345678 open=2100 lseek=870") == 0);
}

/** @brief 扫描结束一行 INFO：结果、条数、耗时（微秒）；耗时是目录读取等花掉的假 DWT 时间（DIAG-6）。 */
static void test_scan_end_info_reports_result_count_and_time(void)
{
    const log_line_t *line;

    world_reset();
    g_dir_read_cost_us = 100U;
    scan(3U, 2U); /* 根目录 3 个文件 + Sub + 结束 = 5 次读取，Sub 里 2 个文件 + 结束 = 3 次读取 */

    assert(log_count_prefix("Scan done") == 1U);
    line = log_last_prefix("Scan done");
    assert(line->level == SERVICE_LOG_LEVEL_INFO);
    assert(strcmp(line->tag, "Catalog") == 0);
    assert(strcmp(line->text, "Scan done res=ok n=5 us=800") == 0);
}

/** @brief 每次重扫各记一行，条数与耗时按本轮算，不带上轮的值。 */
static void test_each_rescan_logs_its_own_scan_end_line(void)
{
    world_reset();
    g_dir_read_cost_us = 100U;
    scan(3U, 0U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=3 us=400") == 0);

    scan(1U, 0U);
    assert(log_count_prefix("Scan done") == 2U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=1 us=200") == 0);
}

/** @brief 周期计数器在扫描中途回绕，耗时仍是真实差值。 */
static void test_scan_time_survives_cycle_counter_wrap(void)
{
    world_reset();
    g_dir_read_cost_us = 100U;
    g_cycles = UINT32_MAX - 250U;
    scan(3U, 2U);

    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=5 us=800") == 0);
}

/** @brief 没有 Music/ 是成功的空表：res=ok n=0。 */
static void test_scan_end_info_for_missing_music_directory(void)
{
    world_reset();
    g_music_missing = true;
    scan(0U, 0U);

    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=0 us=0") == 0);
}

/** @brief 表满截断仍是 ok，条数是收录的 12000。 */
static void test_scan_end_info_for_truncated_catalog(void)
{
    world_reset();
    scan(12500U, 0U);

    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=12000 us=0") == 0);
}

/** @brief 目录读取失败：扫描返回错误，结束行写 res=dfs_error 与已收录的条数。 */
static void test_scan_end_info_for_directory_read_failure(void)
{
    world_reset();
    g_read_dir_fail = true;

    scan(3U, 0U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=dfs_error n=0 us=0") == 0);
    assert(log_count_prefix("Scan done") == 1U);
    assert(catalog_generation() == 0U);
}

/**
 * @brief 打开时得到 NO_MEDIA / NOT_READY / INVALID_HANDLE（卷没了或正在对账，拔卡的正常竞态）：返回空结果，
 *        `catalog_track_text_last_read_unavailable()` 告诉调用者这一次是卷不可用（前台流程据此不记 WARN、不缓存）；
 *        随后一次正常读取把标志清掉。
 */
static void test_text_read_reports_a_volume_unavailable_open_to_the_caller_and_the_flag_is_cleared_by_the_next_read(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (unsigned index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        world_reset();
        scan(3U, 0U);
        g_log_count = 0U;
        g_locator_failed = true;
        g_locator_failed_status = unavailable[index];

        assert(read_text(1U) == NULL);
        assert(catalog_track_text_last_read_unavailable());
        assert(log_count_prefix("Name fail") == 0U);
    }

    world_reset();
    scan(3U, 0U);
    g_locator_failed = true;
    g_locator_failed_status = SERVICE_NO_MEDIA;
    assert(read_text(0U) == NULL);
    assert(catalog_track_text_last_read_unavailable());
    g_locator_failed = false;
    assert(read_text(0U) != NULL);
    assert(!catalog_track_text_last_read_unavailable());
}

/**
 * @brief 打开得到 SERVICE_BUSY（文件槽用尽，瞬时的）：结果标明 `OpenBusy`，调用者过约 10 ms 重试、不缓存（ARC-27）；
 *        其他打开失败（如介质错误）不是 BUSY，同样标明打开失败，但调用者不重试。
 */
static void test_text_read_marks_a_busy_open_so_the_caller_can_retry_and_not_cache(void)
{
    const CatalogTrackText_ResultTypeDef *result;

    world_reset();
    scan(3U, 0U);
    g_locator_failed = true;
    g_locator_failed_status = SERVICE_BUSY;
    result = read_text(1U);
    assert(result != NULL);
    assert(result->OpenFailed);
    assert(result->OpenBusy);

    g_locator_failed_status = SERVICE_ERROR;
    result = read_text(1U);
    assert(result != NULL);
    assert(result->OpenFailed);
    assert(!result->OpenBusy);

    g_locator_failed = false;
    result = read_text(1U);
    assert(result != NULL);
    assert(!result->OpenFailed);
    assert(!result->OpenBusy);
}

/**
 * @brief 曲库发布后叫醒前台任务一次：GUI 不会重写没变的需求，前台要主动重新处理最新需求（#202 E 节）。
 *        被丢弃的扫描（卷变了）和失败的扫描不发布，也不叫醒；每次成功发布各叫醒一次。
 */
static void test_publishing_wakes_the_foreground_exactly_once_and_a_discarded_scan_does_not(void)
{
    world_reset();
    g_foreground_wakes = 0U;
    scan(3U, 0U);
    assert(catalog_generation() != 0U);
    assert(g_foreground_wakes == 1U);

    scan(4U, 0U); /* 重扫再发布一次：再叫醒一次。 */
    assert(g_foreground_wakes == 2U);

    world_reset();
    g_foreground_wakes = 0U;
    g_root_count = 3U;
    g_swap_after_reads = 2U; /* 扫描途中换卡：丢弃，不发布，不叫醒；之后卡还在，重新开始并发布才叫醒。 */
    g_volume.Mounted = true;
    (void)run_until_log("Scan done res=aborted");
    assert(catalog_generation() == 0U);
    assert(g_foreground_wakes == 0U);
    (void)run_to_idle();
    assert(catalog_generation() != 0U);
    assert(g_foreground_wakes == 1U);

    world_reset();
    g_foreground_wakes = 0U;
    g_read_dir_fail = true; /* 读目录失败：不发布。 */
    scan(3U, 0U);
    assert(catalog_generation() == 0U);
    assert(g_foreground_wakes == 0U);
}

/**
 * @brief 日志里有没有 ERROR 级的行。
 * @return 有为真。
 */
static bool log_has_error(void)
{
    for (uint32_t i = 0U; i < g_log_count; i++)
    {
        if (g_log[i].level == SERVICE_LOG_LEVEL_ERROR)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief 日志里 ERROR 级的行数。
 * @return 行数。
 */
static uint32_t log_error_count(void)
{
    uint32_t count = 0U;

    for (uint32_t i = 0U; i < g_log_count; i++)
    {
        if (g_log[i].level == SERVICE_LOG_LEVEL_ERROR)
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief 打开 Music/ 就得到 NO_MEDIA / NOT_READY / INVALID_HANDLE（卷没了或正在对账）：不是“没有 Music 目录”，
 *        丢弃本次扫描，不发布（曲库保持无效），等卷事件重新扫描（#196 的错误分类，#211）。
 *        第一次失败发生在对账之前，所以再开一次（它在 Filesystem 入口等闸）才下结论。
 *        卡被拔走（卷序号变了、卷不在）：安静丢弃，不记 ERROR，等卷事件。
 *        卡还在（卷序号没变且卷仍挂着）：卡还在、读不了，没有卷事件可等，记一条 ERROR 要求重插卡，且不再自己重试。
 *        其他失败（这里的 SERVICE_ERROR）仍按空库发布，沿用 STOR-7。
 */
static void test_scan_is_discarded_when_the_volume_is_unavailable_on_root_open(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (size_t index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        /* 卡被拔走。 */
        world_reset();
        scan(3U, 0U);
        assert(catalog_generation() != 0U);
        g_log_count = 0U;
        g_open_dir_calls = 0U;
        g_open_root_status = unavailable[index];
        g_unavailable_removes_card = true;
        g_volume.Sequence += 2U; /* 换卡，扫描随之开始 */

        (void)run_to_idle();

        assert(g_open_dir_calls == 1U); /* 卡走了：下一步先比对卷序号，变了就丢弃，不再重试 */
        assert(catalog_generation() == 0U);
        assert(count_now() == 0U);
        assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
        assert(log_count_prefix("Music Directory Open Failed") == 0U);
        assert(!log_has_error());

        /* 卡还在、读不了。 */
        world_reset();
        scan(3U, 0U);
        g_log_count = 0U;
        g_open_dir_calls = 0U;
        g_open_root_status = unavailable[index];
        g_volume.Sequence += 2U;

        (void)run_to_idle();

        assert(g_open_dir_calls == 2U); /* 卷序号没变：第一次失败发生在对账之前，再开一次（等闸）才下结论 */
        assert(catalog_generation() == 0U);
        assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
        assert(log_has_error());
        assert(strcmp(log_last_prefix("Catalog scan aborted")->text,
                      "Catalog scan aborted but the card is still there; reinsert the card.") == 0);
        assert(log_last_prefix("Catalog scan aborted")->level == SERVICE_LOG_LEVEL_ERROR);

        /* 没有新的卷序号就不再重试，也不再记日志；换了卡（卷序号变了）才重新开始。 */
        g_log_count = 0U;
        g_open_dir_calls = 0U;
        for (uint32_t lap = 0U; lap < 20U; lap++)
        {
            assert(step_once() == BACKGROUND_WAIT_FOREVER);
        }
        assert(g_open_dir_calls == 0U);
        assert(g_log_count == 0U);

        g_open_root_status = SERVICE_OK;
        g_volume.Sequence += 2U;
        (void)run_to_idle();
        assert(catalog_generation() != 0U);
        assert(count_now() == 3U);
    }

    world_reset();
    g_open_root_status = SERVICE_ERROR;
    scan(3U, 0U);
    assert(catalog_generation() != 0U); /* 其他失败按空库发布。 */
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=0 us=0") == 0);
}

/**
 * @brief 在途调用被拔卡打断时，第一次打开 Music/ 失败发生在对账之前；再开一次（它在 Filesystem 入口等闸，等到对账结束）
 *        就恢复正常（例如只是一次没有重新初始化的假抖动）：扫描照常完成并发布，不当作丢弃。
 */
static void test_open_directory_that_fails_once_as_unavailable_is_retried_and_the_scan_goes_on(void)
{
    world_reset();
    g_open_root_status = SERVICE_NO_MEDIA;
    g_open_root_status_times = 1U;

    scan(3U, 0U);

    assert(g_open_dir_calls == 2U);
    assert(catalog_generation() != 0U);
    assert(count_now() == 3U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=3 us=0") == 0);
    assert(!log_has_error());
}

/**
 * @brief 子目录同样：第一次打开得到卷不可用，再开一次恢复正常，子目录里的曲目照常收录。
 */
static void test_sub_directory_that_fails_once_as_unavailable_is_retried_too(void)
{
    world_reset();
    g_open_sub_status = SERVICE_NOT_READY;
    g_open_sub_status_times = 1U;

    scan(2U, 3U);

    assert(count_now() == 5U);
    assert(!log_has_error());
}

/**
 * @brief 扫描到一半读目录得到卷不可用：先关目录，再丢弃已经收录的条目，不发布；
 *        打开子目录时卷不可用同样丢弃整次扫描，而不是跳过这个子目录继续。
 *        卡被拔走时安静丢弃（不记 ERROR）；卡还在时记 ERROR 要求重插卡。
 */
static void test_scan_is_discarded_when_the_volume_goes_away_mid_scan(void)
{
    /* 读到一半：卡被拔走。 */
    world_reset();
    scan(3U, 0U);
    g_log_count = 0U;
    g_root_count = 5U;
    g_read_dir_status = SERVICE_NO_MEDIA;
    g_read_dir_status_after = 2U;
    g_dir_reads = 0U;
    g_unavailable_removes_card = true;
    g_close_calls = 0U;
    g_volume.Sequence += 2U;

    (void)run_to_idle();

    assert(g_close_calls == 1U); /* 出错之后关目录（它的结果这时已不影响结论） */
    assert(catalog_generation() == 0U);
    assert(count_now() == 0U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
    assert(log_count_prefix("Music catalog dfs failed") == 0U);
    assert(!log_has_error());

    /* 读到一半：卡还在、读不了。 */
    world_reset();
    scan(3U, 0U);
    g_log_count = 0U;
    g_root_count = 5U;
    g_read_dir_status = SERVICE_NO_MEDIA;
    g_read_dir_status_after = 2U;
    g_dir_reads = 0U;
    g_volume.Sequence += 2U;

    (void)run_to_idle();

    assert(catalog_generation() == 0U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
    assert(log_count_prefix("Music catalog dfs failed") == 0U);
    assert(log_count_prefix("Catalog scan aborted but the card is still there") == 1U);

    /* 打开子目录时卷不可用。 */
    world_reset();
    scan(3U, 2U);
    g_log_count = 0U;
    g_open_sub_status = SERVICE_NOT_READY;
    g_unavailable_removes_card = true;
    g_volume.Sequence += 2U;

    (void)run_to_idle();

    assert(catalog_generation() == 0U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
    assert(log_count_prefix("Sub Directory Open Failed") == 0U);
    assert(!log_has_error());
}

/* ---- 发布规则、卷序号、一次读全、曲库短锁（STOR-1、#196） ---- */

static uint32_t g_hook_generation;   /* 读目录钩子里看到的代次 */
static bool g_hook_found;            /* 读目录钩子里看到的“位置 0 有效” */
static char g_hook_path[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];
static uint32_t g_hook_calls;

/** @brief 扫描途中以读者身份调一次一次读全，记下看到的东西。 */
static void reader_during_scan_hook(void)
{
    Catalog_TrackTypeDef track;

    g_hook_found = catalog_read_track(0U, &track);
    g_hook_generation = track.Generation;
    snprintf(g_hook_path, sizeof(g_hook_path), "%s", track.Path);
    g_hook_calls++;
}

/** @brief 开始扫描就把代次置 0：重扫期间读者看到 0，不碰表，上一份曲库的路径也不会漏出来。 */
static void test_generation_is_zero_while_rescanning(void)
{
    uint32_t old_generation;

    world_reset();
    scan(3U, 0U);
    old_generation = catalog_generation();
    assert(old_generation != 0U);

    g_read_hook = reader_during_scan_hook;
    g_hook_calls = 0U;
    scan(5U, 0U); /* 换卡之后重扫 */
    g_read_hook = NULL;

    assert(g_hook_calls > 0U);
    assert(g_hook_generation == 0U);
    assert(!g_hook_found);
    assert(g_hook_path[0] == '\0');
    assert(count_now() == 5U);
    assert(catalog_generation() == old_generation + 1U);
}

/** @brief 代次只在发布时加 1：重扫、作废都不重复，作废不把计数拨回去。 */
static void test_generation_only_increases_on_publish_and_never_repeats(void)
{
    uint32_t previous = 0U;

    world_reset();
    for (unsigned round = 0U; round < 4U; round++)
    {
        scan(2U + round, 0U);
        assert(catalog_generation() > previous);
        previous = catalog_generation();
        unmount_card(); /* 卷消失：代次读作 0，计数不拨回 */
        assert(catalog_generation() == 0U);
    }
    assert(previous == 4U);

    scan(1U, 0U);
    assert(catalog_generation() == 5U);
}

/** @brief 取代次时卷序号不符就算 0：没有任何作废调用，换卡或拔卡后读者也看到 0。 */
static void test_generation_reads_zero_when_the_volume_sequence_differs(void)
{
    Catalog_TrackTypeDef track;

    world_reset();
    scan(3U, 0U);
    assert(catalog_generation() != 0U);
    assert(catalog_read_track(1U, &track));

    g_volume.Sequence += 2U; /* 换卡：序号跳 2，没有任何作废调用 */
    assert(catalog_generation() == 0U);
    assert(!catalog_read_track(1U, &track));
    assert(track.Generation == 0U);
    assert(track.Count == 0U);
    assert(track.Path[0] == '\0');

    g_volume.Sequence -= 2U;
    g_volume.Mounted = false; /* 拔卡：卷不在 */
    assert(catalog_generation() == 0U);
}

/** @brief 一次读全：代次、条数、路径、定位符来自同一次拷贝；越界位置带回代次和条数，路径为空。 */
static void test_one_shot_read_returns_generation_count_path_and_locator(void)
{
    Catalog_TrackTypeDef track;
    char expected[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

    world_reset();
    scan(3U, 2U);

    assert(catalog_read_track(1U, &track));
    assert(track.Generation == catalog_generation());
    assert(track.Count == 5U);
    snprintf(expected, sizeof(expected), "Music/t%05u.mp3", 1U);
    assert(strcmp(track.Path, expected) == 0);
    assert(locator_is(&track.Locator, ROOT_ID_BASE + 1U));

    assert(catalog_read_track(4U, &track)); /* 子文件夹里的最后一条 */
    assert(strcmp(track.Path, "Music/Sub/s001.mp3") == 0);
    assert(locator_is(&track.Locator, SUB_ID_BASE + 1U));

    assert(!catalog_read_track(5U, &track)); /* 越界：不是有效位置，但代次和条数仍然有效 */
    assert(track.Generation == catalog_generation());
    assert(track.Count == 5U);
    assert(track.Path[0] == '\0');

    assert(!catalog_read_track(0U, NULL));
}

/** @brief 每个读写曲库的调用对曲库锁拿放各一次；锁在扫描里只在开始和发布两处拿，文件调用与日志都不在锁内。 */
static void test_every_call_takes_and_releases_the_lock_exactly_once(void)
{
    Catalog_TrackTypeDef track;
    FreertosStub_SemaphoreStats before;
    FreertosStub_SemaphoreStats after;

    world_reset();
    before = freertos_stub_semaphore_stats(g_lock);
    scan(3U, 0U); /* 假文件接口和假日志里断言了锁没被持有 */
    after = freertos_stub_semaphore_stats(g_lock);
    assert(after.take_successes - before.take_successes == 2U); /* 开始扫描一次，发布一次 */
    assert(after.give_successes - before.give_successes == 2U);

    before = after;
    (void)catalog_read_track(1U, &track);
    (void)catalog_generation();
    (void)catalog_read_track(2U, &track);
    after = freertos_stub_semaphore_stats(g_lock);
    assert(after.take_successes - before.take_successes == 3U);
    assert(after.give_successes - before.give_successes == 3U);
    assert(!catalog_lock_is_held());
}

/** @brief 拿不到锁就等：读者不会在没有锁的情况下读已发布的状态，等到拿到后数据完整。 */
static void test_reader_waits_for_the_lock_instead_of_reading_without_it(void)
{
    Catalog_TrackTypeDef track;
    FreertosStub_SemaphoreStats before;
    FreertosStub_SemaphoreStats after;

    world_reset();
    scan(3U, 0U);

    before = freertos_stub_semaphore_stats(g_lock);
    freertos_stub_semaphore_fail_next_takes(g_lock, 2U);
    assert(catalog_read_track(2U, &track));
    after = freertos_stub_semaphore_stats(g_lock);

    assert(track.Count == 3U);
    assert(locator_is(&track.Locator, 2U));
    assert(after.take_calls - before.take_calls == 3U);
    assert(after.take_successes - before.take_successes == 1U);
    assert(after.give_successes - before.give_successes == 1U);
    assert(after.last_timeout_ticks == portMAX_DELAY);
}

/** @brief 扫描途中换卡（读目录时卷序号变了）：每步比对卷序号，变了就关目录（返回值不看）、丢弃，不发布，不记 ERROR；卡还在就重新开始。 */
static void test_card_swapped_mid_scan_discards_without_publishing(void)
{
    world_reset();
    g_swap_after_reads = 3U;
    g_close_status = SERVICE_ERROR; /* 关目录的返回值不看 */
    g_root_count = 6U;
    g_volume.Mounted = true;

    (void)run_until_log("Scan done res=aborted");

    assert(g_close_calls == 1U);
    assert(catalog_generation() == 0U);
    assert(count_now() == 0U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
    assert(!log_has_error());

    /* 卡还在：看到新的卷序号，从头重新开始，这次发布全部 6 条。 */
    g_close_status = SERVICE_OK;
    (void)run_to_idle();
    assert(catalog_generation() != 0U);
    assert(count_now() == 6U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=6 us=0") == 0);
    assert(log_count_prefix("Scan done") == 2U);
}

/** @brief 扫描做完、发布之前换卡：发布时比对卷序号不一致，旧卡的结果不发布；新卡的扫描照常发布。 */
static void test_card_swapped_just_before_publish_does_not_publish(void)
{
    world_reset();
    g_swap_on_close = true;
    g_root_count = 3U;
    g_volume.Mounted = true;

    (void)run_until_log("Scan done res=aborted");

    assert(catalog_generation() == 0U);
    assert(count_now() == 0U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
    assert(!log_has_error());

    g_swap_on_close = false; /* 新卡：同样的扫描，序号与开始时一致，发布 */
    (void)run_to_idle();
    assert(catalog_generation() != 0U);
    assert(count_now() == 3U);
}

/** @brief 卷不在（序号变过、没有挂载）：不扫描、不碰文件接口、不发布，作业只等卷事件。 */
static void test_unmounted_volume_is_never_scanned_or_published(void)
{
    world_reset();
    g_volume.Mounted = false;
    g_root_count = 3U;

    assert(step_once() == BACKGROUND_WAIT_FOREVER);

    assert(catalog_generation() == 0U);
    assert(g_open_dir_calls == 0U);
    assert(log_count_prefix("Scan done") == 0U);
}

/** @brief 错误表：SERVICE_BUSY 过约 10 ms 重试，不算失败（打开根目录、读目录、关目录各一处）。 */
static void test_busy_is_retried_after_about_10_ms_and_is_not_a_failure(void)
{
    TickType_t start;

    world_reset();
    g_open_root_busy = 2U;
    g_read_busy = 1U;
    g_close_busy = 1U;
    start = xTaskGetTickCount();

    scan(3U, 0U);

    assert(catalog_generation() != 0U);
    assert(count_now() == 3U);
    assert((TickType_t)(xTaskGetTickCount() - start) >= (4U * pdMS_TO_TICKS(CATALOG_BUSY_RETRY_MS)));
    assert(log_count_prefix("Music Directory Open Failed") == 0U);
    assert(log_count_prefix("Music catalog dfs failed") == 0U);
    assert(!log_has_error());
    assert(strncmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=3", 20U) == 0);
}

/** @brief 重试等的时候卷序号变了：作废这一轮，不再用旧的卷序号继续试。 */
static void test_busy_retry_stops_when_the_volume_changes(void)
{
    world_reset();
    g_open_root_busy = 1000U;
    g_busy_swaps_volume = true;
    g_root_count = 3U;
    g_volume.Mounted = true;

    (void)run_until_log("Scan done res=aborted");

    assert(g_open_root_busy == 999U); /* 只试了一次 */
    assert(catalog_generation() == 0U);
    /* 耗时是从开始到结束的总时间，含那次 10 ms 的重试等待。 */
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=10000") == 0);
    assert(log_count_prefix("Music Directory Open Failed") == 0U);
}

/** @brief 错误表：关目录时卷不可用同样丢弃；卡被拔走时安静，卡还在读不了时记一条 ERROR 要求重插卡。 */
static void test_volume_unavailable_on_close_discards_the_scan(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (size_t index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        world_reset();
        g_close_status = unavailable[index];
        g_unavailable_removes_card = true;

        scan(3U, 0U);

        assert(catalog_generation() == 0U);
        assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
        assert(!log_has_error());

        world_reset();
        g_close_status = unavailable[index];

        scan(3U, 0U);

        assert(catalog_generation() == 0U);
        assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
        assert(log_count_prefix("Catalog scan aborted but the card is still there") == 1U);
    }
}

/** @brief 错误表：打开根目录的其他错误按空库发布并记 WARN（沿用 STOR-7）。 */
static void test_other_root_open_error_publishes_an_empty_library_with_warn(void)
{
    world_reset();
    g_open_root_status = SERVICE_ERROR;

    scan(3U, 0U);

    assert(catalog_generation() != 0U);
    assert(count_now() == 0U);
    assert(log_count_prefix("Music Directory Open Failed.") == 1U);
    assert(log_last_prefix("Music Directory Open Failed.")->level == SERVICE_LOG_LEVEL_WARN);
    assert(!log_has_error());
}

/** @brief 错误表：打开子目录的其他错误跳过这个子目录，记 WARN，其余照常发布。 */
static void test_other_sub_open_error_skips_that_directory_with_warn(void)
{
    world_reset();
    g_open_sub_status = SERVICE_ERROR;

    scan(3U, 2U);

    assert(catalog_generation() != 0U);
    assert(count_now() == 3U);
    assert(log_count_prefix("Sub Directory Open Failed.") == 1U);
    assert(log_last_prefix("Sub Directory Open Failed.")->level == SERVICE_LOG_LEVEL_WARN);
    assert(!log_has_error());
}

/** @brief 错误表：读目录的其他错误不发布，只记一条 ERROR（带发生在读和状态数值）；之后不再自己重试。 */
static void test_other_read_error_does_not_publish_and_logs_error(void)
{
    world_reset();
    g_read_dir_fail = true;

    scan(3U, 0U);

    assert(catalog_generation() == 0U);
    assert(log_count_prefix("Music catalog dfs failed at=read status=1") == 1U); /* SERVICE_ERROR 是 1 */
    assert(log_last_prefix("Music catalog dfs failed")->level == SERVICE_LOG_LEVEL_ERROR);
    assert(log_error_count() == 1U); /* 原来是 `Music Directory Read Failed.` 加 `Music catalog dfs failed.` 两条 */
}

/** @brief 错误表：关目录的其他错误不发布，只记一条 ERROR（带发生在关和状态数值）。 */
static void test_other_close_error_does_not_publish_and_logs_error(void)
{
    world_reset();
    g_close_status = SERVICE_ERROR;

    scan(3U, 0U);

    assert(catalog_generation() == 0U);
    assert(log_count_prefix("Music catalog dfs failed at=close status=1") == 1U);
    assert(log_error_count() == 1U);
    assert(strncmp(log_last_prefix("Scan done")->text, "Scan done res=dfs_error", 23U) == 0);
}

/** @brief 错误表：条目满发布截断结果（代次非 0，条数 12000）。 */
static void test_full_catalog_publishes_the_truncated_result(void)
{
    world_reset();
    scan(12500U, 0U);

    assert(catalog_generation() != 0U);
    assert(count_now() == 12000U);
}

/* ---- 扫描单步（ARC-22、#196） ---- */

/** @brief 一步读一条目录项，一步最多一次文件调用（`step_once()` 对每一步都断言）；总调用数与目录树一致，不加批量读目录。 */
static void test_scan_makes_one_directory_call_per_step_and_reads_one_entry_per_step(void)
{
    uint32_t steps;

    world_reset();
    g_root_count = 3U;
    g_sub_count = 2U;
    g_volume.Mounted = true;

    steps = run_to_idle();

    assert(g_open_dir_calls == 2U);  /* Music、Music/Sub */
    assert(g_dir_reads == 8U);       /* 根目录 3 个文件、Sub、结束标记；Sub 里 2 个文件、结束标记 */
    assert(g_close_calls == 2U);
    assert(steps >= 12U);            /* 12 次目录调用各占一步 */
    assert(count_now() == 5U);
}

/**
 * @brief 目录树遍历结果与改成单步之前一致：同一棵树（嵌套、隐藏目录、非 mp3 文件、大写扩展名、空目录）经旧的同步扫描
 *        与新的单步扫描得到同一份有序列表。期望的顺序是用旧实现在同一棵树上跑出来的（一次性对照，不是用新代码重算）：
 *        文件按读到的顺序收录，子目录入栈，后入栈的先扫（栈顺序）。
 */
static void test_scan_traverses_a_nested_tree_in_the_established_order(void)
{
    static const TreeDir tree[] = {
        {"Music", {"a.mp3", "B/", "c.mp3", "D/", ".hidden/", "notes.txt", "E.MP3", "F/", NULL}},
        {"Music/B", {"b1.mp3", "G/", "b2.mp3", NULL}},
        {"Music/B/G", {"g1.mp3", NULL}},
        {"Music/D", {"d1.mp3", "H/", NULL}},
        {"Music/D/H", {"h1.mp3", "h2.mp3", NULL}},
        {"Music/F", {NULL}},
    };
    static const char *const expected[] = {
        "Music/a.mp3", "Music/c.mp3", "Music/E.MP3", "Music/D/d1.mp3", "Music/D/H/h1.mp3",
        "Music/D/H/h2.mp3", "Music/B/b1.mp3", "Music/B/b2.mp3", "Music/B/G/g1.mp3",
    };
    Catalog_TrackTypeDef track;

    world_reset();
    g_tree = tree;
    g_tree_count = sizeof(tree) / sizeof(tree[0]);

    scan(0U, 0U);

    assert(count_now() == (sizeof(expected) / sizeof(expected[0])));
    for (uint16_t index = 0U; index < (sizeof(expected) / sizeof(expected[0])); index++)
    {
        assert(catalog_read_track(index, &track));
        assert(strcmp(track.Path, expected[index]) == 0);
    }
    assert(g_open_dir_calls == 6U); /* 六个目录各开一次，隐藏目录 .hidden 没有开 */
    assert(g_close_calls == 6U);
    assert(!log_has_error());
}

/**
 * @brief 目录打不开的 WARN 带节流：一轮扫描里一堆子目录都打不开，只记第一条，其余只计数，汇总进 `Scan done` 行的 `skipped=`
 *        （目录很多的卡上原来每个目录一条 WARN，会塞满 Service Log 的消息池，DIAG-3）；其余收录照常，不是 ERROR。
 */
static void test_sub_directory_open_warnings_are_throttled_to_one_per_scan(void)
{
    static const TreeDir tree[] = {
        {"Music", {"a.mp3", "D1/", "D2/", "D3/", "D4/", "D5/", "b.mp3", NULL}},
    };
    Catalog_TrackTypeDef track;

    world_reset();
    g_tree = tree;
    g_tree_count = 1U;

    scan(0U, 0U);

    assert(count_now() == 2U); /* 五个子目录都不在树里，打不开；两个文件照常收录 */
    assert(catalog_read_track(1U, &track));
    assert(log_count_prefix("Sub Directory Open Failed.") == 1U);
    assert(log_last_prefix("Sub Directory Open Failed.")->level == SERVICE_LOG_LEVEL_WARN);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=2 us=0 skipped=4") == 0);
    assert(log_error_count() == 0U);

    /* 下一轮从零数：再一次只记一条。 */
    g_log_count = 0U;
    scan(0U, 0U);
    assert(log_count_prefix("Sub Directory Open Failed.") == 1U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=2 us=0 skipped=4") == 0);
}

/**
 * @brief 一轮扫描最多三行日志（消息池核算的上界）：第一条 WARN、一条 ERROR、`Scan done`。这里让两个子目录打不开（第二个只计数）、
 *        第三个子目录读失败：WARN 只有一条，ERROR 只有一条（带 `at=read`），结束行带 `skipped=1`。
 */
static void test_a_scan_logs_at_most_three_lines_even_when_everything_goes_wrong(void)
{
    static const TreeDir tree[] = {
        {"Music", {"a.mp3", "D1/", "D2/", "D3/", NULL}},
        {"Music/D1", {"x.mp3", NULL}},
    };

    world_reset();
    g_tree = tree;
    g_tree_count = sizeof(tree) / sizeof(tree[0]);
    g_read_dir_status = SERVICE_ERROR; /* 根目录读了 5 次（a.mp3、三个子目录、结束标记）之后的读都失败：D1 的第一次读 */
    g_read_dir_status_after = 5U;

    scan(0U, 0U); /* 栈后进先出：先试 D3（不在树里，WARN），再 D2（不在，计数），最后 D1（打开成功，读失败） */

    assert(g_log_count == 3U);
    assert(log_count_prefix("Sub Directory Open Failed.") == 1U);
    assert(log_count_prefix("Music catalog dfs failed at=read status=1") == 1U);
    assert(log_count_prefix("Scan done res=dfs_error n=1 us=0 skipped=1") == 1U);
    assert(catalog_generation() == 0U);
}

/** @brief 没有活时作业只等通知：卷没挂、扫描已发布、扫描已放弃，每一步都不碰文件接口、不拿曲库锁、不投日志。 */
static void test_idle_scan_step_waits_for_a_notification_and_does_nothing(void)
{
    FreertosStub_SemaphoreStats before;
    FreertosStub_SemaphoreStats after;

    world_reset();
    g_volume.Mounted = false;
    g_volume.Sequence = 0U; /* 开机至今没有卡 */
    before = freertos_stub_semaphore_stats(g_lock);
    for (uint32_t lap = 0U; lap < 5U; lap++)
    {
        assert(step_once() == BACKGROUND_WAIT_FOREVER);
    }
    after = freertos_stub_semaphore_stats(g_lock);
    assert(g_open_dir_calls == 0U);
    assert(g_dir_reads == 0U);
    assert(g_log_count == 0U);
    assert(after.take_calls == before.take_calls);

    scan(2U, 0U); /* 扫完发布之后同样只等通知 */
    g_open_dir_calls = 0U;
    g_dir_reads = 0U;
    g_log_count = 0U;
    for (uint32_t lap = 0U; lap < 5U; lap++)
    {
        assert(step_once() == BACKGROUND_WAIT_FOREVER);
    }
    assert(g_open_dir_calls == 0U);
    assert(g_dir_reads == 0U);
    assert(g_log_count == 0U);
}

/** @brief SERVICE_BUSY（槽用尽）不算失败：这一步返回约 10 ms 让后台任务睡一会儿再叫它，不在步里阻塞等待。 */
static void test_busy_makes_the_step_return_about_10_ms_instead_of_blocking(void)
{
    uint32_t first_wait = 0U;

    world_reset();
    g_open_root_busy = 1U;
    g_root_count = 1U;
    g_volume.Mounted = true;

    for (uint32_t lap = 0U; (lap < 10U) && (first_wait == 0U); lap++)
    {
        first_wait = step_once();
    }
    assert(first_wait == 10U);
    assert(xTaskGetTickCount() >= 10U); /* 睡的时间由后台任务的等待负责，步本身没有 vTaskDelay（桩里 tick 只随驱动前进） */
    (void)run_to_idle();
    assert(count_now() == 1U);
}

/* ---- 曲库短锁的持锁统计（Debug 构建，DIAG-6） ---- */

/** @brief 节拍没到不投；到了在放锁之后投一行，格式 `n=<拿取次数> hold=<最长持有>us`，标签 CATLOCK。 */
static void test_lock_stats_report_one_line_per_period_after_the_lock_is_released(void)
{
    Catalog_TrackTypeDef track;
    const log_line_t *line;
    char expected[48];
    uint32_t takes;

    world_reset();
    g_volume_status_cost_us = 40U; /* 曲库在锁内读一次卷状态，所以每次读者调用持锁 40 µs */

    freertos_stub_set_tick(pdMS_TO_TICKS(CATALOG_LOCK_STATS_PERIOD_MS) - 1U);
    scan(3U, 0U);
    (void)catalog_read_track(0U, &track);
    assert(log_count_prefix("n=") == 0U); /* 节拍没到 */

    freertos_stub_set_tick(pdMS_TO_TICKS(CATALOG_LOCK_STATS_PERIOD_MS));
    (void)catalog_read_track(1U, &track); /* 这次放锁后发现节拍已到 */
    takes = freertos_stub_semaphore_stats(g_lock).take_successes;

    assert(log_count_prefix("n=") == 1U);
    line = log_last_prefix("n=");
    assert(line->level == SERVICE_LOG_LEVEL_INFO);
    assert(strcmp(line->tag, "CATLOCK") == 0);
    snprintf(expected, sizeof(expected), "n=%lu hold=40us", (unsigned long)takes);
    assert(strcmp(line->text, expected) == 0);

    (void)catalog_read_track(1U, &track); /* 同一节拍内不再投 */
    assert(log_count_prefix("n=") == 1U);

    freertos_stub_set_tick(2U * pdMS_TO_TICKS(CATALOG_LOCK_STATS_PERIOD_MS));
    (void)catalog_generation();
    assert(log_count_prefix("n=") == 2U); /* 下一个节拍再投一行，数值是开机以来的最大值，不清零 */
    assert(strstr(log_last_prefix("n=")->text, "hold=40us") != NULL);
}

/** @brief 最长持有取开机以来的最大值，且只量持锁区间：锁外的耗时（扫描读目录）不计入。 */
static void test_lock_stats_hold_is_the_longest_locked_interval_only(void)
{
    Catalog_TrackTypeDef track;

    world_reset();
    g_dir_read_cost_us = 5000U; /* 文件调用慢，但不在锁内 */
    scan(3U, 0U);
    g_volume_status_cost_us = 25U;
    (void)catalog_read_track(0U, &track);
    g_volume_status_cost_us = 90U;
    (void)catalog_generation();
    g_volume_status_cost_us = 10U;
    (void)catalog_read_track(0U, &track);

    freertos_stub_set_tick(pdMS_TO_TICKS(CATALOG_LOCK_STATS_PERIOD_MS));
    (void)catalog_generation();

    assert(strstr(log_last_prefix("n=")->text, "hold=90us") != NULL);
}

/** @brief 汇总行放得进 Service Log 的 128 B。 */
static void test_lock_stats_line_fits_the_log_capacity(void)
{
    Catalog_TrackTypeDef track;

    world_reset();
    g_volume_status_cost_us = 3999999U; /* 4 s 以内量周期，数很大也要放得下 */
    freertos_stub_set_tick(pdMS_TO_TICKS(CATALOG_LOCK_STATS_PERIOD_MS));
    (void)catalog_read_track(0U, &track);

    assert(strlen(log_last_prefix("n=")->text) < CATALOG_LOCK_STATS_LINE_BYTES);
}

/**
 * @brief 运行全部用例。
 * @return 0 表示全部通过（失败由 assert 中止）。
 */
int main(void)
{
    test_catalog_limit_12000();
    test_scan_attaches_locator_to_each_entry();
    test_locator_is_valid_only_within_one_generation();
    test_text_read_opens_by_locator();
    test_locator_mismatch_falls_back_to_path_open();
    test_missing_locator_falls_back_to_path_open();
    test_each_locator_miss_is_logged();
    test_locator_open_failure_is_not_retried_by_path();
    test_text_read_treats_a_volume_unavailable_open_as_no_result();
    test_text_read_with_unknown_catalog_index_returns_null();
    test_name_info_separates_open_from_tag_read();
    test_name_fallback_info_has_open();
    test_window_summary_silent_at_the_budgets();
    test_window_summary_fires_on_slow_single_open();
    test_window_summary_fires_on_slow_window();
    test_window_summary_still_fires_on_warn_reason();
    test_window_summary_defensive_threshold_is_kept();
    test_window_summary_fits_log_capacity();
    test_stream_info_line();
    test_scan_end_info_reports_result_count_and_time();
    test_each_rescan_logs_its_own_scan_end_line();
    test_scan_time_survives_cycle_counter_wrap();
    test_scan_end_info_for_missing_music_directory();
    test_scan_end_info_for_truncated_catalog();
    test_scan_end_info_for_directory_read_failure();
    test_scan_is_discarded_when_the_volume_is_unavailable_on_root_open();
    test_scan_is_discarded_when_the_volume_goes_away_mid_scan();
    test_text_read_reports_a_volume_unavailable_open_to_the_caller_and_the_flag_is_cleared_by_the_next_read();
    test_text_read_marks_a_busy_open_so_the_caller_can_retry_and_not_cache();
    test_publishing_wakes_the_foreground_exactly_once_and_a_discarded_scan_does_not();
    test_open_directory_that_fails_once_as_unavailable_is_retried_and_the_scan_goes_on();
    test_sub_directory_that_fails_once_as_unavailable_is_retried_too();
    test_generation_is_zero_while_rescanning();
    test_generation_only_increases_on_publish_and_never_repeats();
    test_generation_reads_zero_when_the_volume_sequence_differs();
    test_one_shot_read_returns_generation_count_path_and_locator();
    test_every_call_takes_and_releases_the_lock_exactly_once();
    test_reader_waits_for_the_lock_instead_of_reading_without_it();
    test_card_swapped_mid_scan_discards_without_publishing();
    test_card_swapped_just_before_publish_does_not_publish();
    test_unmounted_volume_is_never_scanned_or_published();
    test_busy_is_retried_after_about_10_ms_and_is_not_a_failure();
    test_busy_retry_stops_when_the_volume_changes();
    test_volume_unavailable_on_close_discards_the_scan();
    test_other_root_open_error_publishes_an_empty_library_with_warn();
    test_other_sub_open_error_skips_that_directory_with_warn();
    test_other_read_error_does_not_publish_and_logs_error();
    test_other_close_error_does_not_publish_and_logs_error();
    test_full_catalog_publishes_the_truncated_result();
    test_scan_makes_one_directory_call_per_step_and_reads_one_entry_per_step();
    test_scan_traverses_a_nested_tree_in_the_established_order();
    test_sub_directory_open_warnings_are_throttled_to_one_per_scan();
    test_a_scan_logs_at_most_three_lines_even_when_everything_goes_wrong();
    test_idle_scan_step_waits_for_a_notification_and_does_nothing();
    test_busy_makes_the_step_return_about_10_ms_instead_of_blocking();
    test_lock_stats_report_one_line_per_period_after_the_lock_is_released();
    test_lock_stats_hold_is_the_longest_locked_interval_only();
    test_lock_stats_line_fits_the_log_capacity();
    puts("catalog_open: ok");
    return 0;
}
