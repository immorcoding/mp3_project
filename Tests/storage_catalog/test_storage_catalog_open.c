/**
 * @file test_storage_catalog_open.c
 * @brief 曲库按文件定位符打开的主机测试（STOR-17、ARC-19、DIAG-4、DIAG-5）。
 * @note  链接真实的 storage_catalog.c、storage_sheet.c、storage_track_open.c、storage_track_text.c 与
 *        storage_track_diag.c；Filesystem Service、Log Service 与 DWT Adapter 由本文件的假实现替换，
 *        只实现既有公开函数。经曲库扫描、按曲库下标读曲目文本、诊断出口观察行为。
 */

#include "APP/tasks/playback/playback_task_config.h"
#include "APP/tasks/storage/catalog/storage_catalog.h"
#include "APP/tasks/storage/catalog/storage_listbuffer.h"
#include "APP/tasks/storage/catalog/storage_namebuffer.h"
#include "APP/tasks/storage/catalog/storage_sheet.h"
#include "APP/tasks/storage/catalog/storage_track_diag.h"
#include "APP/tasks/storage/catalog/storage_track_text.h"

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_text.h"
#include "Service/log/log_service.h"

#include "freertos_stub.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- 常量核对（STOR-17、DIAG-4） ---- */
_Static_assert(STORAGE_CATALOG_MUSIC_MAX_NUM == 12000U, "catalog holds 12000 tracks (STOR-17)");
_Static_assert(STORAGE_CATALOG_MUSIC_MAX_NUM <= UINT16_MAX, "catalog index must fit uint16_t");
_Static_assert(STORAGE_CATALOG_MUSIC_POOL_SIZE == (4U * 1024U * 1024U), "path pool stays 4 MiB (#172 G)");
_Static_assert((STORAGE_CATALOG_MUSIC_POOL_SIZE / STORAGE_CATALOG_MUSIC_MAX_NUM) >= 300U,
               "pool leaves about 350 B of path per track");
_Static_assert(STORAGE_CATALOG_OPEN_BUDGET_US == 30000U, "single open budget is 30 ms (DIAG-4)");
_Static_assert(STORAGE_CATALOG_WINDOW_BUDGET_US == 500000U, "window budget is 0.5 s (DIAG-4)");
_Static_assert(STORAGE_CATALOG_OPEN_BUDGET_US < STORAGE_CATALOG_TRACK_BUDGET_US,
               "single open budget is tighter than the T_data/2 defensive threshold");
_Static_assert(STORAGE_CATALOG_WINDOW_BUDGET_US < STORAGE_CATALOG_TRACK_BUDGET_US,
               "window budget is tighter than the T_data/2 defensive threshold");
_Static_assert(STORAGE_CATALOG_TRACK_BUDGET_US == ((PLAYBACK_MACHINE_DATA_TIMEOUT_MS * 1000U) / 2U),
               "T_data/2 defensive threshold is kept (DIAG-4)");

#define ROOT_ID_BASE 0U
#define SUB_ID_BASE  100000U
#define LOG_MAX      64U
#define LOG_TEXT_MAX 160U

/* ---- 假 DWT：1 MHz，一个周期即 1 µs ---- */
static uint32_t g_cycles;

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
 * @brief Storage Task 的 SD 就绪查询的替身：恒为就绪（窗口槽的 request 只在就绪时接受）。
 * @return 恒为 true。
 */
bool storage_task_sd_is_ready(void)
{
    return true;
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
    if (g_music_missing)
    {
        return SERVICE_ERROR;
    }
    g_open_dir_calls++;
    if (strcmp(path, "Music") == 0)
    {
        const Service_StatusTypeDef scripted = take_scripted_status(&g_open_root_status, &g_open_root_status_times);

        if (scripted != SERVICE_OK)
        {
            return scripted;
        }
        g_dir_token = 1U;
    }
    else if ((strcmp(path, "Music/Sub") == 0) && (g_sub_count != 0U))
    {
        const Service_StatusTypeDef scripted = take_scripted_status(&g_open_sub_status, &g_open_sub_status_times);

        if (scripted != SERVICE_OK)
        {
            return scripted;
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

    g_cycles += g_dir_read_cost_us;
    g_dir_reads++;
    if ((g_read_dir_status != SERVICE_OK) && (g_dir_reads > g_read_dir_status_after))
    {
        return g_read_dir_status;
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
 * @return 恒为 SERVICE_OK。
 */
Service_StatusTypeDef Service_Filesystem_CloseDirectory(Service_Filesystem_DirectoryHandleTypeDef directory)
{
    (void)directory;
    return SERVICE_OK;
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
    g_cycles = 1000U;
    g_log_count = 0U;
    build_file_bytes();
}

/**
 * @brief 扫描合成曲库。
 * @param[in] root Music/ 下文件数。
 * @param[in] sub Music/Sub/ 下文件数。
 */
static void scan(uint32_t root, uint32_t sub)
{
    g_root_count = root;
    g_sub_count = sub;
    assert(storage_catalog_music_init() == STORAGE_OK);
}

/**
 * @brief 找出路径等于 path 的曲库下标。
 * @param[in] path 曲库相对路径。
 * @return 下标；没有则中止。
 */
static uint16_t catalog_index_of(const char *path)
{
    char buffer[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

    for (uint16_t i = 0U; i < storage_catalog_index_num(); i++)
    {
        assert(storage_catalog_copy_path(i, buffer, sizeof(buffer)) == STORAGE_OK);
        if (strcmp(buffer, path) == 0)
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

    assert(storage_catalog_index_num() == 12000U);
    assert(storage_catalog_copy_locator(0U, &locator));
    assert(locator_is(&locator, 0U));
    assert(storage_catalog_copy_locator(11999U, &locator));
    assert(locator_is(&locator, 11999U));
    assert(!storage_catalog_copy_locator(12000U, &locator));
    assert(log_count_prefix("Music Catalog Full.") == 1U);
}

/** @brief 扫描时每条随附文件定位符，子文件夹里的也一样。 */
static void test_scan_attaches_locator_to_each_entry(void)
{
    Service_Filesystem_FileLocatorTypeDef locator;
    char path[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

    world_reset();
    scan(3U, 2U);

    assert(storage_catalog_index_num() == 5U);
    for (unsigned i = 0U; i < 3U; i++)
    {
        snprintf(path, sizeof(path), "Music/t%05u.mp3", i);
        assert(storage_catalog_copy_locator(catalog_index_of(path), &locator));
        assert(locator_is(&locator, ROOT_ID_BASE + i));
    }
    for (unsigned i = 0U; i < 2U; i++)
    {
        snprintf(path, sizeof(path), "Music/Sub/s%03u.mp3", i);
        assert(storage_catalog_copy_locator(catalog_index_of(path), &locator));
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
    first_generation = storage_catalog_generation();
    assert(storage_catalog_copy_locator(3U, &locator));

    assert(storage_catalog_invalidate() == STORAGE_OK);
    assert(!storage_catalog_copy_locator(3U, &locator));

    scan(2U, 0U);
    assert(storage_catalog_generation() != first_generation);
    assert(!storage_catalog_copy_locator(3U, &locator)); /* 新代次只有 2 条 */
    assert(storage_catalog_copy_locator(1U, &locator));
}

/** @brief 读曲目文本按定位符打开：不走路径，打开耗时单独给出。 */
static void test_text_read_opens_by_locator(void)
{
    const StorageTrackText_ResultTypeDef *track;

    world_reset();
    scan(8U, 0U);
    g_locator_cost_us = 1234U;
    g_path_cost_us = 99999U;

    track = storage_track_text_read(7U, 5U);

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
    const StorageTrackText_ResultTypeDef *track;
    const log_line_t *line;

    world_reset();
    scan(8U, 0U);
    g_stale_id = 5U;
    g_locator_cost_us = 1000U;
    g_path_cost_us = 2000U;
    g_log_cost_us = 700U; /* Locator miss 的日志投递耗时不属于打开 */

    track = storage_track_text_read(7U, 5U);

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
    assert(strcmp(line->text, "Locator miss pos=7") == 0);
}

/** @brief 扫描时没取到定位符（全零）与核对不符同样处理。 */
static void test_missing_locator_falls_back_to_path_open(void)
{
    const StorageTrackText_ResultTypeDef *track;

    world_reset();
    g_zero_locator_id = 2U;
    scan(4U, 0U);

    track = storage_track_text_read(2U, 2U);

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

    (void)storage_track_text_read(10U, 1U);
    (void)storage_track_text_read(11U, 1U);
    (void)storage_track_text_read(12U, 3U); /* 3 号核对通过，不记 */

    assert(log_count_prefix("Locator miss") == 2U);
    assert(strcmp(log_last_prefix("Locator miss")->text, "Locator miss pos=11") == 0);
}

/** @brief 其他失败（槽用尽等）不是核对不符：不退回路径打开，也不记 Locator miss。 */
static void test_locator_open_failure_is_not_retried_by_path(void)
{
    const StorageTrackText_ResultTypeDef *track;

    world_reset();
    scan(4U, 0U);
    g_locator_failed = true;

    track = storage_track_text_read(1U, 1U);

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

        assert(storage_track_text_read(1U, 1U) == NULL);
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

    assert(storage_track_text_read(5U, 5U) == NULL);
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
    StorageTrackText_ResultTypeDef track;
    const uint32_t start = storage_track_diag_begin();

    memset(&track, 0, sizeof(track));
    track.OpenUs = open_us;
    track.OpenFailed = open_failed;
    track.Text.Result = STORAGE_CATALOG_TEXT_TAGGED;
    track.Text.Reads = 1U;
    g_cycles += total_us;
    storage_track_diag_row_done(start, &track);
}

/** @brief 名称槽 INFO 带 open=，标签读取耗时 us= 不含打开。 */
static void test_name_slot_info_separates_open_from_tag_read(void)
{
    StorageTrackText_ResultTypeDef track;
    const log_line_t *line;
    uint32_t start;

    world_reset();
    memset(&track, 0, sizeof(track));
    track.OpenUs = 1234U;
    track.Text.Result = STORAGE_CATALOG_TEXT_TAGGED;
    track.Text.Reads = 2U;
    start = storage_track_diag_begin();
    g_cycles += 5000U;

    storage_track_diag_name_done(7U, start, &track);

    line = log_last_prefix("Tag idx=");
    assert(line->level == SERVICE_LOG_LEVEL_INFO);
    assert(strcmp(line->text, "Tag idx=7 src=title reads=2 open=1234 us=3766") == 0);
}

/** @brief 回退行同样带 open=。 */
static void test_name_slot_fallback_info_has_open(void)
{
    StorageTrackText_ResultTypeDef track;
    uint32_t start;

    world_reset();
    memset(&track, 0, sizeof(track));
    track.OpenUs = 800U;
    track.Text.Result = STORAGE_CATALOG_TEXT_NO_ID3V2;
    track.Text.Reads = 1U;
    start = storage_track_diag_begin();
    g_cycles += 1000U;

    storage_track_diag_name_done(3U, start, &track);

    assert(strcmp(log_last_prefix("Tag idx=")->text, "Tag idx=3 src=file why=no_id3v2 reads=1 open=800 us=200") == 0);
}

/** @brief 窗口汇总：单行打开恰为 30 ms、整窗恰为 0.5 s 都不触发。 */
static void test_window_summary_silent_at_the_budgets(void)
{
    world_reset();
    storage_track_diag_window_begin();
    for (unsigned i = 0U; i < 12U; i++)
    {
        window_row(STORAGE_CATALOG_OPEN_BUDGET_US, 41666U, false);
    }
    storage_track_diag_window_end(); /* 12 × 41666 = 499992 µs */

    assert(log_count_prefix("Tag win") == 0U);
}

/** @brief 窗口汇总：单行打开超过 30 ms 触发，并给出最长单行打开。 */
static void test_window_summary_fires_on_slow_single_open(void)
{
    const log_line_t *line;

    world_reset();
    storage_track_diag_window_begin();
    for (unsigned i = 0U; i < 11U; i++)
    {
        window_row(2000U, 5000U, false);
    }
    window_row(STORAGE_CATALOG_OPEN_BUDGET_US + 1U, 40000U, false);
    storage_track_diag_window_end();

    assert(log_count_prefix("Tag win") == 1U);
    line = log_last_prefix("Tag win");
    assert(line->level == SERVICE_LOG_LEVEL_INFO);
    assert(strstr(line->text, "omax=30001") != NULL);
}

/** @brief 窗口汇总：整窗超过 0.5 s 触发，即使每行打开都很快。 */
static void test_window_summary_fires_on_slow_window(void)
{
    world_reset();
    storage_track_diag_window_begin();
    for (unsigned i = 0U; i < 12U; i++)
    {
        window_row(5000U, 41667U, false);
    }
    storage_track_diag_window_end(); /* 12 × 41667 = 500004 µs */

    assert(log_count_prefix("Tag win") == 1U);
    assert(log_last_prefix("Tag win")->level == SERVICE_LOG_LEVEL_INFO);
}

/** @brief 窗口汇总：WARN 级标签原因照旧触发，级别为 WARN。 */
static void test_window_summary_still_fires_on_warn_reason(void)
{
    world_reset();
    storage_track_diag_window_begin();
    window_row(1000U, 2000U, false);
    window_row(0U, 500U, true);
    storage_track_diag_window_end();

    assert(log_count_prefix("Tag win") == 1U);
    assert(log_last_prefix("Tag win")->level == SERVICE_LOG_LEVEL_WARN);
}

/** @brief 窗口汇总：T_data/2 仍是单行防御阈值（此处用单行总耗时超限，打开很快）。 */
static void test_window_summary_defensive_threshold_is_kept(void)
{
    world_reset();
    storage_track_diag_window_begin();
    window_row(1000U, STORAGE_CATALOG_TRACK_BUDGET_US + 1U, false);
    storage_track_diag_window_end();

    assert(log_count_prefix("Tag win") == 1U);
}

/** @brief 汇总行最长的现实取值也放得进 Service_Log 的 128 B。 */
static void test_window_summary_fits_log_capacity(void)
{
    world_reset();
    storage_track_diag_window_begin();
    for (unsigned i = 0U; i < 12U; i++)
    {
        window_row(250000U, 300000U, true);
    }
    storage_track_diag_window_end();

    assert(strstr(log_last_prefix("Tag win")->text, "rerr=") != NULL);
}

/** @brief Stream INFO：位置、请求的偏移、打开与 lseek 耗时。 */
static void test_stream_info_line(void)
{
    const log_line_t *line;

    world_reset();
    storage_track_diag_stream_done(12U, 345678U, 2100U, 870U);

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
    g_root_count = 3U;
    g_read_dir_fail = true;

    assert(storage_catalog_music_init() == STORAGE_ERROR);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=dfs_error n=0 us=0") == 0);
    assert(log_count_prefix("Scan done") == 1U);
}

/**
 * @brief 把窗口槽和名字槽绑到桩里的一个假任务上（request 要发通知）。
 */
static void bind_slots(void)
{
    TaskHandle_t task;

    freertos_stub_reset();
    task = freertos_stub_task_create();
    storage_listbuffer_bind(task);
    storage_namebuffer_bind(task);
}

/**
 * @brief 填满一个窗口：request 之后逐圈 load，直到整窗 READY。
 * @param[in] count 请求条数。
 */
static void fill_window(uint16_t count)
{
    uint32_t guard = 0U;

    assert(storage_listbuffer_request(0U, count, 0U) == STORAGE_OK);
    while (storage_listbuffer.Status == STORAGE_LISTBUFFER_PENDING)
    {
        assert(guard++ < 64U);
        assert(storage_listbuffer_load() == STORAGE_OK);
    }
    assert(storage_listbuffer.Status == STORAGE_LISTBUFFER_READY);
}

/**
 * @brief 卷序号变了（卸载、换卡）时收掉窗口槽：已经 READY 但 GUI 还没取走的窗口也要收掉（代次 0、长度 0），
 *        否则换卡后 GUI 会把旧卡的行贴出来；正在填的窗口以空窗收尾；空闲的槽不动。
 */
static void test_unavailable_empties_a_ready_window_the_gui_has_not_taken_yet(void)
{
    world_reset();
    bind_slots();
    scan(3U, 0U);

    /* 整窗刚填完（末行读完时卷还在，序号变化在下一圈才被看到）：READY，带着旧代次。 */
    fill_window(3U);
    assert(storage_listbuffer.Length == 3U);
    assert(storage_listbuffer.Generation != 0U);

    storage_listbuffer_complete_unavailable();

    assert(storage_listbuffer.Status == STORAGE_LISTBUFFER_READY);
    assert(storage_listbuffer.Length == 0U);
    assert(storage_listbuffer.Generation == 0U);

    /* 填到一半的窗口：以空窗收尾（原有行为）。 */
    storage_listbuffer.Status = STORAGE_LISTBUFFER_IDLE;
    assert(storage_listbuffer_request(0U, 3U, 0U) == STORAGE_OK);
    assert(storage_listbuffer_load() == STORAGE_OK);
    assert(storage_listbuffer.Status == STORAGE_LISTBUFFER_PENDING);
    storage_listbuffer_complete_unavailable();
    assert(storage_listbuffer.Status == STORAGE_LISTBUFFER_READY);
    assert(storage_listbuffer.Length == 0U);
    assert(storage_listbuffer.Generation == 0U);

    /* 空闲的槽不动。 */
    storage_listbuffer.Status = STORAGE_LISTBUFFER_IDLE;
    storage_listbuffer_complete_unavailable();
    assert(storage_listbuffer.Status == STORAGE_LISTBUFFER_IDLE);
}

/**
 * @brief 名字槽同理：已经 READY 但 GUI 还没取走的名字也收掉（空名、代次 0）；空闲的槽不动。
 */
static void test_unavailable_empties_a_ready_name_the_gui_has_not_taken_yet(void)
{
    world_reset();
    bind_slots();
    scan(3U, 0U);

    assert(storage_namebuffer_request(1U, 0U) == STORAGE_OK);
    storage_namebuffer_load(true);
    assert(storage_namebuffer.Status == STORAGE_NAMEBUFFER_READY);
    assert(storage_namebuffer.Generation != 0U);
    assert(storage_namebuffer.Title[0] != 0);

    storage_namebuffer_complete_unavailable();

    assert(storage_namebuffer.Status == STORAGE_NAMEBUFFER_READY);
    assert(storage_namebuffer.Generation == 0U);
    assert(storage_namebuffer.Title[0] == 0);
    assert(storage_namebuffer.Artist[0] == 0);

    storage_namebuffer.Status = STORAGE_NAMEBUFFER_IDLE;
    storage_namebuffer_complete_unavailable();
    assert(storage_namebuffer.Status == STORAGE_NAMEBUFFER_IDLE);
}

/**
 * @brief 读标签时卷不可用（拔卡的正常竞态）：名字槽以空名收尾，**不记 WARN**；`Name fail` 只留给真正的防御分支
 *        （代次相符却越界）。曲目文本读取用 `storage_track_text_last_read_unavailable()` 告诉调用者这一次是卷不可用。
 */
static void test_name_slot_does_not_warn_when_the_volume_went_away_during_the_read(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (unsigned index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        world_reset();
        bind_slots();
        scan(3U, 0U);
        g_log_count = 0U;
        g_locator_failed = true;
        g_locator_failed_status = unavailable[index];

        assert(storage_namebuffer_request(1U, 0U) == STORAGE_OK);
        storage_namebuffer_load(true);

        assert(storage_namebuffer.Status == STORAGE_NAMEBUFFER_READY);
        assert(storage_namebuffer.Generation == 0U);
        assert(storage_namebuffer.Title[0] == 0);
        assert(storage_track_text_last_read_unavailable());
        assert(log_count_prefix("Name fail") == 0U);
    }

    /* 防御分支仍记 WARN：请求代次非 0 且相符，位置却越界。 */
    world_reset();
    bind_slots();
    scan(3U, 0U);
    g_log_count = 0U;
    assert(storage_namebuffer_request(99U, storage_sheet_generation()) == STORAGE_OK);
    storage_namebuffer_load(true);
    assert(storage_namebuffer.Generation == 0U);
    assert(log_count_prefix("Name fail") == 1U);

    /* 正常读之后标志清掉。 */
    world_reset();
    bind_slots();
    scan(3U, 0U);
    g_locator_failed = true;
    g_locator_failed_status = SERVICE_NO_MEDIA;
    assert(storage_track_text_read(0U, 0U) == NULL);
    assert(storage_track_text_last_read_unavailable());
    g_locator_failed = false;
    assert(storage_track_text_read(0U, 0U) != NULL);
    assert(!storage_track_text_last_read_unavailable());
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
 * @brief 打开 Music/ 就得到 NO_MEDIA / NOT_READY / INVALID_HANDLE（卷没了或正在对账）：不是“没有 Music 目录”，
 *        丢弃本次扫描，不发布（曲库保持无效），不记 ERROR，等卷事件重新扫描（#196 的错误分类，#211）。
 *        其他失败（这里的 SERVICE_ERROR）仍按空库发布，沿用 STOR-7。
 */
static void test_scan_is_discarded_when_the_volume_is_unavailable_on_root_open(void)
{
    static const Service_StatusTypeDef unavailable[] = {SERVICE_NO_MEDIA, SERVICE_NOT_READY, SERVICE_INVALID_HANDLE};

    for (size_t index = 0U; index < sizeof(unavailable) / sizeof(unavailable[0]); index++)
    {
        world_reset();
        scan(3U, 0U);
        assert(storage_sheet_generation() != 0U);
        assert(storage_catalog_invalidate() == STORAGE_OK);
        g_log_count = 0U;
        g_open_dir_calls = 0U;
        g_open_root_status = unavailable[index];

        assert(storage_catalog_music_init() == STORAGE_OK);

        assert(g_open_dir_calls == 2U); /* 第一次失败发生在对账之前，再开一次（在 Filesystem 入口等闸）才下结论 */
        assert(storage_sheet_generation() == 0U);
        assert(storage_catalog_index_num() == 0U);
        assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
        assert(log_count_prefix("Music Directory Open Failed") == 0U);
        assert(!log_has_error());
        /* 序号变了：卡没了，等卷事件；序号没变且卷仍挂着：卡还在、读不了，没有卷事件可等。 */
        assert(storage_catalog_scan_verdict(false) == STORAGE_CATALOG_SCAN_EVENT_PENDING);
        assert(storage_catalog_scan_verdict(true) == STORAGE_CATALOG_SCAN_UNREADABLE);
    }

    world_reset();
    g_open_root_status = SERVICE_ERROR;
    scan(3U, 0U);
    assert(storage_sheet_generation() != 0U); /* 其他失败按空库发布。 */
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
    assert(storage_sheet_generation() != 0U);
    assert(storage_catalog_index_num() == 3U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=ok n=3 us=0") == 0);
    assert(storage_catalog_scan_verdict(true) == STORAGE_CATALOG_SCAN_DONE);
    assert(storage_catalog_scan_verdict(false) == STORAGE_CATALOG_SCAN_DONE);
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

    assert(storage_catalog_index_num() == 5U);
    assert(storage_catalog_scan_verdict(true) == STORAGE_CATALOG_SCAN_DONE);
}

/**
 * @brief 扫描到一半读目录得到卷不可用：丢弃已经收录的条目，不发布，不记 ERROR；
 *        打开子目录时卷不可用同样丢弃整次扫描，而不是跳过这个子目录继续。
 */
static void test_scan_is_discarded_when_the_volume_goes_away_mid_scan(void)
{
    world_reset();
    scan(3U, 0U);
    assert(storage_catalog_invalidate() == STORAGE_OK);
    g_log_count = 0U;
    g_root_count = 5U;
    g_read_dir_status = SERVICE_NO_MEDIA;
    g_read_dir_status_after = 2U;
    g_dir_reads = 0U;

    assert(storage_catalog_music_init() == STORAGE_OK);

    assert(storage_sheet_generation() == 0U);
    assert(storage_catalog_index_num() == 0U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
    assert(log_count_prefix("Music Directory Read Failed") == 0U);
    assert(!log_has_error());
    assert(storage_catalog_scan_verdict(false) == STORAGE_CATALOG_SCAN_EVENT_PENDING);
    assert(storage_catalog_scan_verdict(true) == STORAGE_CATALOG_SCAN_UNREADABLE);

    world_reset();
    scan(3U, 2U);
    assert(storage_catalog_invalidate() == STORAGE_OK);
    g_log_count = 0U;
    g_open_sub_status = SERVICE_NOT_READY;

    assert(storage_catalog_music_init() == STORAGE_OK);

    assert(storage_sheet_generation() == 0U);
    assert(strcmp(log_last_prefix("Scan done")->text, "Scan done res=aborted n=0 us=0") == 0);
    assert(log_count_prefix("Sub Directory Open Failed") == 0U);
    assert(!log_has_error());
    assert(storage_catalog_scan_verdict(false) == STORAGE_CATALOG_SCAN_EVENT_PENDING);
    assert(storage_catalog_scan_verdict(true) == STORAGE_CATALOG_SCAN_UNREADABLE);
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
    test_name_slot_info_separates_open_from_tag_read();
    test_name_slot_fallback_info_has_open();
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
    test_unavailable_empties_a_ready_window_the_gui_has_not_taken_yet();
    test_unavailable_empties_a_ready_name_the_gui_has_not_taken_yet();
    test_name_slot_does_not_warn_when_the_volume_went_away_during_the_read();
    test_open_directory_that_fails_once_as_unavailable_is_retried_and_the_scan_goes_on();
    test_sub_directory_that_fails_once_as_unavailable_is_retried_too();
    puts("storage_catalog_open: ok");
    return 0;
}
