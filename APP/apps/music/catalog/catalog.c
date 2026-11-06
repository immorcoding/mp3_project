/**
 * @file catalog.c
 * @brief 音乐曲库：扫描 SD `Music/` 写入 SDRAM 字符串池，按发布规则交给读者；一份表，模块内一把短锁。
 *
 * @details
 *          锁与发布（STOR-1，#196）：
 *          - 锁是静态互斥量，由 `catalog_boot_init()` 在 Create Task（Boot）里创建，早于创建任何会用曲库的任务。
 *          - 持锁只拷内存：不调文件接口、不投日志、不等别的对象；日志在放锁之后投。
 *          - 已发布的状态只有三个字（代次、条数、发布时的卷序号），都在锁内读写；表本身（路径池、条目、定位符、播放列表）
 *            在代次为 0 的扫描期间由扫描者不拿锁地写，读者看到代次 0 就不碰表。
 *          - 开始扫描：锁内把代次置 0。发布：锁内比对卷序号，一致才写代次和条数。
 *            代次只在发布时加 1，一次开机内不重复。取代次时卷序号不符就算 0。
 *
 *          扫描这时仍由 Storage Task 同步调用，改成单步函数留给后台任务票。发布成功后叫醒前台任务一次（`foreground_task_wake()`，
 *          APP 内部接口），前台流程据此重新处理最新需求。
 */

#include "catalog.h"
#include "catalog_name.h"
#include "catalog_sheet.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "APP/tasks/foreground/foreground_task.h"
#include "APP/tasks/volume/volume_event.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_types.h"
#include "Service/log/log_service.h"

typedef struct
{
    uint32_t Tail; /**< 字符串池已用字节，含每条结尾 '\0'。 */
    char Path[CATALOG_MUSIC_POOL_SIZE];
} Catalog_MusicTypeDef;

typedef struct
{
    uint32_t Offset; /**< 相对 Path 池起点。 */
    uint32_t Length; /**< 路径字节数，不含 '\0'。 */
} Catalog_MusicItemsTypeDef;

/** @brief 已发布的状态；位于内部 .bss，上电为零，只在持锁时读写。 */
typedef struct
{
    uint32_t Generation;     /**< 曲库代次；0 表示没有已发布的曲库（扫描中、已作废、从未发布）。 */
    uint16_t Count;          /**< 已发布的曲库条数；代次为 0 时为 0。 */
    uint32_t VolumeSequence; /**< 发布时的 SD 卷序号；取代次时与现在的卷序号不符就算代次 0。 */
} Catalog_PublishedTypeDef;

static const char catalog_log_tag[] = "Catalog";

static Service_Filesystem_DirectoryHandleTypeDef music_directory;

/** @brief 已发布的状态；只在持曲库锁时访问。 */
static Catalog_PublishedTypeDef catalog_published;

/** @brief 上一次发布用的代次；只在发布时加 1（持锁），所以一次开机内不重复。位于 .bss，上电为零。 */
static uint32_t catalog_generation_counter;

/** @brief 曲库锁（静态互斥量）；`catalog_boot_init()` 创建。 */
static StaticSemaphore_t catalog_lock_storage;
static SemaphoreHandle_t catalog_lock;

/** @brief 本轮扫描的工作下标，上电为零，每次开扫前清零。 */
static uint32_t music_catalog_index;

/** @brief 本轮扫描开始时的 SD 卷序号；发布时与当时的卷序号比对。只在扫描者（Storage Task）访问。 */
static uint32_t music_catalog_scan_sequence;

/** @brief 本轮扫描因为卷不可用（NO_MEDIA、NOT_READY、INVALID_HANDLE）或卷序号变了被丢弃；每次开扫前清零，只在 Storage Task 访问。 */
static bool music_catalog_scan_aborted;

static Catalog_MusicTypeDef MusicCatalogPool
    __attribute__((section(".storage_catalog"), aligned(32)));

static Catalog_MusicItemsTypeDef MusicCatalogIndex[CATALOG_MUSIC_MAX_NUM]
    __attribute__((section(".storage_catalog"), aligned(32)));

/** @brief 与 MusicCatalogIndex 同下标的文件定位符（16 B/条）；扫描时随条目写入，只在同一曲库代次内有效。 */
static Service_Filesystem_FileLocatorTypeDef MusicCatalogLocator[CATALOG_MUSIC_MAX_NUM]
    __attribute__((section(".storage_catalog"), aligned(32)));

static char catalog_dir_stack[CATALOG_DIR_STACK_MAX]
                                     [SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U]
    __attribute__((section(".storage_catalog"), aligned(32)));

/** @brief 本轮扫描开始时的周期计数器读数；与 music_catalog_scan_cycles 一起只在 Storage Task 访问。 */
static uint32_t music_catalog_scan_last_cycles;

/** @brief 本轮扫描累计的周期数（64 位）：DWT 为 32 位，480 MHz 下约 9 s 回绕，扫描可能更长，所以边走边累加。 */
static uint64_t music_catalog_scan_cycles;

#if CATALOG_LOCK_STATS_ENABLE
/* 周期计数器是 32 位的，480 MHz 下约 8.9 s 回绕；区间超过这个 tick 数（4 s，留足余量）就改用 tick 量。 */
#define CATALOG_LOCK_STATS_DWT_LIMIT_TICKS 4000U

_Static_assert(configTICK_RATE_HZ <= 1000000U, "tick fallback converts ticks to microseconds");

/** @brief 一个时间点：周期计数 + tick，用来在周期计数回绕前后都量对。 */
typedef struct
{
    uint32_t cycles;
    TickType_t tick;
} catalog_lock_stamp_t;

/** @brief 曲库锁开机以来的统计；只在持锁时读写。 */
typedef struct
{
    uint32_t acquisitions;       /**< 拿取次数。 */
    uint32_t hold_max_us;        /**< 最长一次持有，微秒。 */
    catalog_lock_stamp_t hold_start; /**< 当前这次持有的起点。 */
    TickType_t report_last_tick; /**< 上一次投汇总的 tick。 */
} catalog_lock_stats_t;

static catalog_lock_stats_t catalog_lock_stats;
#endif

#if CATALOG_LOCK_STATS_ENABLE
/**
 * @brief 取当前时间点。
 * @return 周期计数与 tick。
 */
static catalog_lock_stamp_t catalog_lock_stamp_now(void)
{
    catalog_lock_stamp_t stamp;

    stamp.tick = xTaskGetTickCount();
    stamp.cycles = CortexM7CycleCounter_Read();
    return stamp;
}

/**
 * @brief 两个时间点之间的微秒数。
 * @param[in] from 起点。
 * @param[in] to 终点，不早于起点。
 * @return 区间不足 4 s 且周期计数器可用时按周期计数（微秒精度）；否则按 tick 算（毫秒精度）。结果饱和在 32 位。
 */
static uint32_t catalog_lock_stamp_diff_us(const catalog_lock_stamp_t *from, const catalog_lock_stamp_t *to)
{
    const TickType_t tick_delta = to->tick - from->tick;
    const uint32_t cycles_per_us = CortexM7CycleCounter_GetFrequencyHz() / 1000000U;
    uint64_t microseconds;

    if ((cycles_per_us == 0U) || (tick_delta >= CATALOG_LOCK_STATS_DWT_LIMIT_TICKS))
    {
        microseconds = (uint64_t)tick_delta * (1000000U / (uint32_t)configTICK_RATE_HZ);
        return (microseconds > UINT32_MAX) ? UINT32_MAX : (uint32_t)microseconds;
    }

    return (to->cycles - from->cycles) / cycles_per_us;
}

/**
 * @brief 往行尾追加文本，装不下就截断。
 * @param[in,out] buffer 行缓冲。
 * @param[in] capacity 缓冲容量，含结尾 NUL。
 * @param[in,out] length 当前长度。
 * @param[in] text 以 '\0' 结尾的文本。
 */
static void catalog_lock_line_text(char *buffer, uint32_t capacity, uint32_t *length, const char *text)
{
    while ((*text != '\0') && ((*length + 1U) < capacity))
    {
        buffer[(*length)++] = *text++;
    }
    buffer[*length] = '\0';
}

/**
 * @brief 往行尾追加一个十进制无符号数。不用 snprintf：它在调用者的栈上要几百字节，而汇总可能发生在任何读曲库的任务里。
 * @param[in,out] buffer 行缓冲。
 * @param[in] capacity 缓冲容量，含结尾 NUL。
 * @param[in,out] length 当前长度。
 * @param[in] value 数值。
 */
static void catalog_lock_line_u32(char *buffer, uint32_t capacity, uint32_t *length, uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;

    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    while ((count != 0U) && ((*length + 1U) < capacity))
    {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
}

/**
 * @brief 投一行曲库短锁汇总：`n=<拿取次数> hold=<最长持有微秒>us`，标签 `CATLOCK`。
 * @param[in] acquisitions 开机以来的拿取次数。
 * @param[in] hold_max_us 开机以来的最长持有，微秒。
 * @note  只在放锁之后调用，所以投递日志时调用者不持曲库锁。节拍 `CATALOG_LOCK_STATS_PERIOD_MS`，一次一行。
 */
static void catalog_lock_stats_post(uint32_t acquisitions, uint32_t hold_max_us)
{
    char line[CATALOG_LOCK_STATS_LINE_BYTES];
    uint32_t length = 0U;

    line[0] = '\0';
    catalog_lock_line_text(line, sizeof(line), &length, "n=");
    catalog_lock_line_u32(line, sizeof(line), &length, acquisitions);
    catalog_lock_line_text(line, sizeof(line), &length, " hold=");
    catalog_lock_line_u32(line, sizeof(line), &length, hold_max_us);
    catalog_lock_line_text(line, sizeof(line), &length, "us");
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "CATLOCK", line);
}
#endif /* CATALOG_LOCK_STATS_ENABLE */

/**
 * @brief 等到拿到曲库锁。
 * @note  `INCLUDE_vTaskSuspend` 为 0，`portMAX_DELAY` 只是约 49.7 天的超时，所以超时后继续等：调用者要的是
 *        “等到拿到为止”，不能在没有锁的情况下读写已发布的状态。持锁者只拷内存，等待时间有界。
 *        锁必须已由 `catalog_boot_init()` 创建，否则断言停机。统计开关打开时在拿到锁后记持有起点。
 */
static void catalog_lock_enter(void)
{
    configASSERT(catalog_lock != NULL);
    while (xSemaphoreTake(catalog_lock, portMAX_DELAY) != pdTRUE)
    {
    }
#if CATALOG_LOCK_STATS_ENABLE
    catalog_lock_stats.acquisitions++;
    catalog_lock_stats.hold_start = catalog_lock_stamp_now();
#endif
}

/**
 * @brief 放曲库锁；统计开关打开时先更新最长持有，节拍到了在放锁之后投一行汇总。
 * @note  汇总的两个数在锁内抄下，日志在锁外投，所以持锁期间不投日志。没有读写曲库的调用就没有汇总。
 */
static void catalog_lock_leave(void)
{
    BaseType_t given;
#if CATALOG_LOCK_STATS_ENABLE
    const catalog_lock_stamp_t released = catalog_lock_stamp_now();
    const uint32_t hold_us = catalog_lock_stamp_diff_us(&catalog_lock_stats.hold_start, &released);
    uint32_t report_acquisitions = 0U;
    uint32_t report_hold_max_us = 0U;
    bool report = false;

    if (hold_us > catalog_lock_stats.hold_max_us)
    {
        catalog_lock_stats.hold_max_us = hold_us;
    }
    if ((TickType_t)(released.tick - catalog_lock_stats.report_last_tick) >= pdMS_TO_TICKS(CATALOG_LOCK_STATS_PERIOD_MS))
    {
        catalog_lock_stats.report_last_tick = released.tick;
        report_acquisitions = catalog_lock_stats.acquisitions;
        report_hold_max_us = catalog_lock_stats.hold_max_us;
        report = true;
    }
#endif

    given = xSemaphoreGive(catalog_lock);
    configASSERT(given == pdTRUE);
    (void)given;

#if CATALOG_LOCK_STATS_ENABLE
    if (report)
    {
        catalog_lock_stats_post(report_acquisitions, report_hold_max_us);
    }
#endif
}

/**
 * @brief 创建曲库锁并复位已发布的状态。
 * @note  只在 Create Task（Boot）里调用一次，早于创建任何会读写曲库的任务（Storage Task 及以后的音频读取、前台、后台），
 *        所以其他函数不需要检查“锁建了没有”，漏调用会在第一次拿锁时断言停机。静态互斥量不占堆。
 *        代次计数从 0 重新开始：整个开机只调用一次，所以“一次开机内不重复”成立。
 */
void catalog_boot_init(void)
{
    catalog_lock = xSemaphoreCreateMutexStatic(&catalog_lock_storage);
    configASSERT(catalog_lock != NULL);
    catalog_published.Generation = 0U;
    catalog_published.Count = 0U;
    catalog_published.VolumeSequence = 0U;
    catalog_generation_counter = 0U;
#if CATALOG_LOCK_STATS_ENABLE
    (void)memset(&catalog_lock_stats, 0, sizeof(catalog_lock_stats));
    catalog_lock_stats.report_last_tick = xTaskGetTickCount();
#endif
}

/**
 * @brief 开始计本轮扫描耗时。
 */
static void catalog_scan_clock_start(void)
{
    music_catalog_scan_last_cycles = CortexM7CycleCounter_Read();
    music_catalog_scan_cycles = 0U;
}

/**
 * @brief 把距上次读数过去的周期数累加进本轮扫描耗时。
 * @note  扫描里每次目录打开与读取之后调用；相邻两次调用相隔一次目录操作（毫秒级），远小于 32 位计数器的回绕周期，
 *        所以无符号减法取差值总是对的。
 */
static void catalog_scan_clock_poll(void)
{
    const uint32_t now = CortexM7CycleCounter_Read();

    music_catalog_scan_cycles += (uint32_t)(now - music_catalog_scan_last_cycles);
    music_catalog_scan_last_cycles = now;
}

/**
 * @brief 本轮扫描耗时，微秒。
 * @return 微秒数（饱和在 32 位）；核心频率不可用时为 0。
 */
static uint32_t catalog_scan_clock_us(void)
{
    const uint32_t frequency_hz = CortexM7CycleCounter_GetFrequencyHz();
    uint64_t us;

    if (frequency_hz == 0U)
    {
        return 0U;
    }

    us = (music_catalog_scan_cycles * 1000000ULL) / frequency_hz;
    return (us > UINT32_MAX) ? UINT32_MAX : (uint32_t)us;
}

/**
 * @brief 记扫描结束一行 INFO：结果、条数、耗时（DIAG-6）。
 * @param[in] result 结果名，静态字符串：ok、dfs_error、aborted。
 * @note  常开，每次扫描一行；文本只含结果名与数值。耗时单位微秒，含目录读取、打开与扫描中的任务切换，不含建播放列表。
 *        条数取工作下标：丢弃本次扫描时调用者已把它清零。
 */
static void catalog_scan_log_end(const char *result)
{
    char text[64];

    (void)snprintf(text, sizeof(text), "Scan done res=%s n=%lu us=%lu",
                   result,
                   (unsigned long)music_catalog_index,
                   (unsigned long)catalog_scan_clock_us());
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, catalog_log_tag, text);
}

/**
 * @brief 开始一轮扫描：锁内把代次置 0、条数置 0，记下此刻的卷序号，清扫描工作状态。
 * @note  此后读者看到代次 0，不碰表；扫描者写表不拿锁。卷序号取自 `volume_status()`（读一个字，不拿锁）。
 */
static void catalog_scan_begin(void)
{
    music_catalog_scan_sequence = volume_status(VOLUME_ID_SD).Sequence;

    catalog_lock_enter();
    catalog_published.Generation = 0U;
    catalog_published.Count = 0U;
    catalog_lock_leave();

    music_catalog_index = 0U;
    MusicCatalogPool.Tail = 0U;
    music_catalog_scan_aborted = false;
    catalog_scan_clock_start();
}

/**
 * @brief 发布本轮扫描的结果：锁内比对卷序号，一致才写代次和条数。
 * @return true 已发布（代次加 1，条数写入）；false 卷序号变了或卷已不在，曲库保持代次 0。
 * @note  播放列表在调用前已建好（扫描期间代次为 0，没有读者碰它）。代次只在这里加 1，跳过 0；
 *        同一轮扫描不会发布两次。持锁期间只比对和写三个字。
 */
static bool catalog_scan_publish(void)
{
    bool published = false;

    catalog_lock_enter();
    {
        const Volume_StatusTypeDef volume = volume_status(VOLUME_ID_SD);

        if (volume.Mounted && (volume.Sequence == music_catalog_scan_sequence))
        {
            catalog_generation_counter++;
            if (catalog_generation_counter == 0U)
            {
                catalog_generation_counter = 1U;
            }
            catalog_published.Generation = catalog_generation_counter;
            catalog_published.Count = (uint16_t)music_catalog_index;
            catalog_published.VolumeSequence = music_catalog_scan_sequence;
            published = true;
        }
    }
    catalog_lock_leave();
    return published;
}

/**
 * @brief 扫描中途看一眼卷序号：和开扫时一样才继续。
 * @return true 卷序号没变；false 卷变了（换卡、拔插），本轮扫描作废。
 */
static bool catalog_scan_volume_unchanged(void)
{
    return volume_status(VOLUME_ID_SD).Sequence == music_catalog_scan_sequence;
}

/**
 * @brief 文件调用返回 `SERVICE_BUSY`（槽用尽）后等一会儿再试，不算失败。
 * @return true 可以重试；false 等的时候卷变了，本轮扫描作废（`music_catalog_scan_aborted` 已置位）。
 * @note  现在扫描由 Storage Task 同步调用，所以用任务延时；改成单步函数后由返回值告诉后台任务多久后再叫它。
 */
static bool catalog_scan_wait_before_retry(void)
{
    vTaskDelay(pdMS_TO_TICKS(CATALOG_BUSY_RETRY_MS));
    if (!catalog_scan_volume_unchanged())
    {
        music_catalog_scan_aborted = true;
        return false;
    }
    return true;
}

/**
 * @brief 判断文件名是否以 `.mp3` / `.MP3` 结尾。
 * @param[in] filename 以 '\0' 结尾的文件名；NULL 视为不收录。
 * @return true 长度在 FatFs 名字上限内且扩展名符合；否则 false。
 */
static bool catalog_music_file_strcheck(const char *filename)
{
    uint32_t length;

    if (filename == NULL)
    {
        return false;
    }

    length = (uint32_t)strlen(filename);
    if (length > SERVICE_FILESYSTEM_NAME_MAX_BYTES)
    {
        return false;
    }

    /* 与显示名去扩展名共用同一判断，收录规则不会与显示规则漂移。 */
    return catalog_has_mp3_extension(filename, length);
}

/**
 * @brief 把一条已拼好的 UTF-8 相对路径及其文件定位符写入曲库（不拿锁：扫描期间代次为 0）。
 * @param[in] path UTF-8 相对路径。
 * @param[in] locator 目录扫描时随条目给出的文件定位符，全零表示没有。
 * @return true 已收录；false 条数满或池满。
 */
static bool catalog_music_append(const char *path,
                                 const Service_Filesystem_FileLocatorTypeDef *locator)
{
    uint32_t length;
    uint32_t needed;

    if (music_catalog_index >= CATALOG_MUSIC_MAX_NUM)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                               catalog_log_tag,
                               "Music Catalog Full.");
        return false;
    }

    length = (uint32_t)strlen(path);
    needed = length + 1U;
    if ((MusicCatalogPool.Tail + needed) > CATALOG_MUSIC_POOL_SIZE)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                               catalog_log_tag,
                               "Music Catalog Pool Full.");
        return false;
    }

    (void)memcpy(&MusicCatalogPool.Path[MusicCatalogPool.Tail], path, needed);
    MusicCatalogIndex[music_catalog_index].Offset = MusicCatalogPool.Tail;
    MusicCatalogIndex[music_catalog_index].Length = length;
    MusicCatalogLocator[music_catalog_index] = *locator;
    MusicCatalogPool.Tail += needed;
    music_catalog_index++;
    return true;
}

/**
 * @brief 拼接父目录与目录项名，禁止末尾 `/`。
 * @param[out] destination 输出缓冲。
 * @param[in] destination_size 输出缓冲容量，含结尾 NUL。
 * @param[in] directory 父目录路径。
 * @param[in] name 目录项名。
 * @return true 拼接成功且放得下；false 拼接失败或被截断。
 */
static bool catalog_music_join_path(char *destination,
                                    uint32_t destination_size,
                                    const char *directory,
                                    const char *name)
{
    int written;

    written = snprintf(destination, destination_size, "%s/%s", directory, name);
    return (written >= 0) && ((uint32_t)written < destination_size);
}

/**
 * @brief 打开一个目录（单次）；`SERVICE_BUSY`（槽用尽）等约 10 ms 重试，不算失败。
 * @param[in] path 目录路径。
 * @param[out] directory 目录句柄。
 * @return 最后一次调用的状态；重试途中卷序号变了时返回 `SERVICE_BUSY` 且 `music_catalog_scan_aborted` 已置位。
 */
static Service_StatusTypeDef catalog_scan_open_directory_once(const char *path,
                                                              Service_Filesystem_DirectoryHandleTypeDef *directory)
{
    Service_StatusTypeDef status;

    for (;;)
    {
        status = Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, path, directory);
        if ((status != SERVICE_BUSY) || !catalog_scan_wait_before_retry())
        {
            return status;
        }
    }
}

/**
 * @brief 打开一个目录；`SERVICE_BUSY` 重试，卷不可用时再开一次。
 * @param[in] path 目录路径。
 * @param[out] directory 目录句柄。
 * @return 最后一次调用的状态；重试途中卷序号变了时返回 `SERVICE_BUSY` 且 `music_catalog_scan_aborted` 已置位。
 * @note  在途调用被拔卡打断时，第一次失败发生在卷任务对账之前，卷序号还没变。路径打开没有句柄可借来等闸，
 *        所以再开一次：它在 Filesystem 入口等闸，等到本轮对账结束，才有资格下结论——卡没了（卷已卸载，
 *        再次得到卷不可用）或卡还在（照常打开，扫描继续）（#211）。
 */
static Service_StatusTypeDef catalog_scan_open_directory(const char *path,
                                                         Service_Filesystem_DirectoryHandleTypeDef *directory)
{
    Service_StatusTypeDef status = catalog_scan_open_directory_once(path, directory);

    if (!music_catalog_scan_aborted && Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        status = catalog_scan_open_directory_once(path, directory);
    }
    return status;
}

/**
 * @brief 读一条目录项；`SERVICE_BUSY` 等约 10 ms 重试，不算失败。
 * @param[in] directory 目录句柄。
 * @param[out] entry 目录项。
 * @return 最后一次调用的状态；重试途中卷序号变了时返回 `SERVICE_BUSY` 且 `music_catalog_scan_aborted` 已置位。
 */
static Service_StatusTypeDef catalog_scan_read_directory(Service_Filesystem_DirectoryHandleTypeDef directory,
                                                         Service_Filesystem_DirectoryEntryTypeDef *entry)
{
    Service_StatusTypeDef status;

    for (;;)
    {
        status = Service_Filesystem_ReadDirectory(directory, entry);
        catalog_scan_clock_poll();
        if ((status != SERVICE_BUSY) || !catalog_scan_wait_before_retry())
        {
            return status;
        }
    }
}

/**
 * @brief 关一个目录；`SERVICE_BUSY` 等约 10 ms 重试，不算失败。
 * @param[in] directory 目录句柄。
 * @return 最后一次调用的状态；重试途中卷序号变了时返回 `SERVICE_BUSY` 且 `music_catalog_scan_aborted` 已置位。
 */
static Service_StatusTypeDef catalog_scan_close_directory(Service_Filesystem_DirectoryHandleTypeDef directory)
{
    Service_StatusTypeDef status;

    for (;;)
    {
        status = Service_Filesystem_CloseDirectory(directory);
        if ((status != SERVICE_BUSY) || !catalog_scan_wait_before_retry())
        {
            return status;
        }
    }
}

/**
 * @brief 用路径栈遍历 `Music/`，同时只开一个目录句柄；每个 `.mp3` 条目连同它的文件定位符写入曲库。
 * @return CATALOG_OK 扫描完成（含没有 `Music/`、子目录打不开、表满截断）；CATALOG_ERROR 目录读取或关闭失败，
 *         或本轮扫描被丢弃（`music_catalog_scan_aborted` 置位，不记 ERROR）。
 * @note  错误分类（#196，沿用 STOR-7）：`SERVICE_BUSY` 过约 10 ms 重试；`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 丢弃本轮；
 *        打开根目录的其他错误按空库、打开子目录的其他错误跳过该子目录，各记 WARN；读或关目录的其他错误不发布，记 ERROR；
 *        条目满、池满发布截断结果。每读一条前比对卷序号，变了就关目录（返回值不看）并丢弃本轮。
 */
static Catalog_StatusTypeDef catalog_music_dfs(void)
{
    uint32_t stack_top = 1U;
    Catalog_StatusTypeDef result = CATALOG_OK;

    (void)memset(catalog_dir_stack[0], 0, sizeof(catalog_dir_stack[0]));
    (void)memcpy(catalog_dir_stack[0], "Music", sizeof("Music"));

    while (stack_top > 0U)
    {
        char current[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];
        Service_StatusTypeDef open_status;
        Service_StatusTypeDef read_status;
        Service_StatusTypeDef close_status;

        stack_top--;
        (void)memcpy(current,
                      catalog_dir_stack[stack_top],
                      sizeof(current));

        open_status = catalog_scan_open_directory(current, &music_directory);
        if (open_status != SERVICE_OK)
        {
            catalog_scan_clock_poll();
            if (music_catalog_scan_aborted || Service_Filesystem_StatusIsVolumeUnavailable(open_status))
            {
                /* 卷没了或正在对账：不是“没有 Music 目录”，也不是子目录坏了；丢弃本次扫描，等卷事件。 */
                music_catalog_scan_aborted = true;
                return CATALOG_ERROR;
            }
            if ((stack_top == 0U) && (strcmp(current, "Music") == 0))
            {
                (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                                       catalog_log_tag,
                                       "Music Directory Open Failed.");
                return CATALOG_OK;
            }

            (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                                   catalog_log_tag,
                                   "Sub Directory Open Failed.");
            continue;
        }

        catalog_scan_clock_poll();

        for (;;)
        {
            Service_Filesystem_DirectoryEntryTypeDef entry;
            char child[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];

            if (!catalog_scan_volume_unchanged())
            {
                /* 换卡或拔插：关目录（返回值不看）并丢弃；卡还在的话，调用者看到新卷序号会重新开始。 */
                (void)Service_Filesystem_CloseDirectory(music_directory);
                music_catalog_scan_aborted = true;
                return CATALOG_ERROR;
            }

            read_status = catalog_scan_read_directory(music_directory, &entry);
            if (read_status != SERVICE_OK)
            {
                if (music_catalog_scan_aborted || Service_Filesystem_StatusIsVolumeUnavailable(read_status))
                {
                    music_catalog_scan_aborted = true; /* 同上：不是文件错误，丢弃本次扫描。 */
                }
                else
                {
                    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                           catalog_log_tag,
                                           "Music Directory Read Failed.");
                }
                result = CATALOG_ERROR;
                break;
            }

            if (entry.Name[0] == '\0')
            {
                break;
            }

            if (entry.Name[0] == '.')
            {
                continue;
            }

            if (!catalog_music_join_path(child, sizeof(child), current, entry.Name))
            {
                continue;
            }

            if (entry.IsDirectory)
            {
                if (stack_top >= CATALOG_DIR_STACK_MAX)
                {
                    (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                                           catalog_log_tag,
                                           "Music Directory Stack Full.");
                    continue;
                }

                (void)memcpy(catalog_dir_stack[stack_top], child, sizeof(child));
                stack_top++;
                continue;
            }

            if (!catalog_music_file_strcheck(entry.Name))
            {
                continue;
            }

            if (!catalog_music_append(child, &entry.Locator))
            {
                result = CATALOG_OK;
                stack_top = 0U;
                break;
            }
        }

        close_status = catalog_scan_close_directory(music_directory);
        if (close_status != SERVICE_OK)
        {
            if (music_catalog_scan_aborted || Service_Filesystem_StatusIsVolumeUnavailable(close_status))
            {
                music_catalog_scan_aborted = true;
            }
            result = CATALOG_ERROR;
            break;
        }

        if (result != CATALOG_OK)
        {
            break;
        }
    }

    return result;
}

/**
 * @brief 最近一次扫描结束后，Storage Task 该怎么对待。
 * @param[in] volume_unchanged 扫描结束时 SD 卷仍挂着且卷序号与 Storage 已处理的相同。
 * @return 扫描没有被丢弃：`CATALOG_SCAN_DONE`。丢弃了且卷变了（卡没了或换了）：`CATALOG_SCAN_EVENT_PENDING`，
 *         等卷事件重扫，不要就绪。丢弃了但卷没变：`CATALOG_SCAN_UNREADABLE`——卡还在、设备出错，没有边沿就没有卷事件，
 *         等也等不来，调用者要报错（卷任务对账之前的调用已在 Filesystem 入口等过闸，所以卷序号此时是结论）。
 */
Catalog_ScanVerdictTypeDef catalog_scan_verdict(bool volume_unchanged)
{
    if (!music_catalog_scan_aborted)
    {
        return CATALOG_SCAN_DONE;
    }

    return volume_unchanged ? CATALOG_SCAN_UNREADABLE : CATALOG_SCAN_EVENT_PENDING;
}

/**
 * @brief 扫描已挂载 SD 的 `Music/`，建播放列表，再按发布规则发布。
 * @return CATALOG_OK 扫描完成并发布（含空目录或表满截断），或本轮扫描被丢弃（卷不可用、卷序号变了，曲库保持代次 0）；
 *         CATALOG_ERROR 读取或关闭目录失败（不发布）。
 * @note 仅 Storage Task 在卷就绪（卷事件）之后调用。不 memset 整池。开始时锁内把代次置 0，之后写表不拿锁；
 *       发布时锁内比对卷序号，一致才写代次和条数。丢弃本轮不是文件错误，不记 ERROR。
 */
Catalog_StatusTypeDef catalog_music_init(void)
{
    catalog_scan_begin();
    if (catalog_music_dfs() != CATALOG_OK)
    {
        if (music_catalog_scan_aborted)
        {
            /* 扫描期间卷消失、正在对账或卷序号变了：丢弃已收录的条目，不发布，曲库保持代次 0，等卷事件重新扫描。
             * 这不是文件错误（#195 第 9 条、#196），所以返回成功、不记 ERROR。 */
            music_catalog_index = 0U;
            catalog_scan_log_end("aborted");
            return CATALOG_OK;
        }

        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               catalog_log_tag,
                               "Music catalog dfs failed.");
        catalog_scan_log_end("dfs_error");
        return CATALOG_ERROR;
    }

    catalog_sheet_fill((uint16_t)music_catalog_index);
    if (!catalog_scan_publish())
    {
        /* 扫描做完时卷序号已经变了（或卷已不在）：这份结果属于旧卡，不发布。 */
        music_catalog_scan_aborted = true;
        music_catalog_index = 0U;
        catalog_scan_log_end("aborted");
        return CATALOG_OK;
    }

    catalog_scan_log_end("ok");

    /* 发布之后叫醒前台任务一次：GUI 不会重写没变的需求，前台要主动重新处理最新需求（曲名答过代次 0 的、窗口格里还是空窗的）。
     * 在放锁之后叫醒，不在持锁时（持锁只拷内存）。被丢弃或失败的扫描不发布，也不叫醒。 */
    foreground_task_wake();

    return CATALOG_OK;
}

/**
 * @brief 预留的 Books Catalog 初始化。
 * @return 当前尚未扫描，返回 CATALOG_OK。
 */
Catalog_StatusTypeDef catalog_books_init(void)
{
    return CATALOG_OK;
}

/**
 * @brief 按产品卷顺序重建 Catalog。
 * @return 任一子库失败则 CATALOG_ERROR。
 */
Catalog_StatusTypeDef catalog_init(void)
{
    if (catalog_music_init() != CATALOG_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               catalog_log_tag,
                               "Music catalog initialization failed.");
        return CATALOG_ERROR;
    }

    // if (catalog_books_init() != CATALOG_OK)
    // {
    //     (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
    //                            catalog_log_tag,
    //                            "Books catalog initialization failed.");
    //     return CATALOG_ERROR;
    // }

    return CATALOG_OK;
}

/**
 * @brief 拔卡或卸载时作废曲库：锁内把代次和条数置 0。
 * @return CATALOG_OK。
 * @note  不清表，不动代次计数（下一次发布仍加 1，不会重复）。即使不调用它，取代次时卷序号不符也会算 0，
 *        所以这是让已发布的状态尽快归零，不是正确性所必需。
 */
Catalog_StatusTypeDef catalog_invalidate(void)
{
    catalog_lock_enter();
    catalog_published.Generation = 0U;
    catalog_published.Count = 0U;
    catalog_lock_leave();
    return CATALOG_OK;
}

/**
 * @brief 取已发布的代次（调用者已持锁）：卷序号不符或卷不在就算 0。
 * @return 代次；0 表示不可用。
 */
static uint32_t catalog_generation_locked(void)
{
    const Volume_StatusTypeDef volume = volume_status(VOLUME_ID_SD);

    if ((catalog_published.Generation == 0U) || !volume.Mounted ||
        (volume.Sequence != catalog_published.VolumeSequence))
    {
        return 0U;
    }

    return catalog_published.Generation;
}

/**
 * @brief 返回当前曲库代次。
 * @return 代次；0 表示曲库不可用（扫描中、已作废、从未发布，或发布后卷序号变了）。
 * @note  锁内比对卷序号，不符就算 0，所以不需要任何任务调用作废。
 */
uint32_t catalog_generation(void)
{
    uint32_t generation;

    catalog_lock_enter();
    generation = catalog_generation_locked();
    catalog_lock_leave();
    return generation;
}

/**
 * @brief 读者一次读全：在同一把锁内完成“播放列表位置到曲库下标”，并拷出代次、条数、路径和定位符。
 * @param[in] position 播放列表位置。
 * @param[out] out 结果，不得为空。代次为 0 时条数为 0、路径为空串、定位符全零；位置越界时代次和条数有效、路径为空串。
 * @return true 位置有效，路径和定位符已拷出；false 曲库不可用（`out->Generation` 为 0）或位置越界。
 * @note  持锁只拷内存（路径约几十字节到 256 B，加 16 B 定位符），不调文件接口、不投日志。代次 0 时不碰表。
 *        所有读曲库的调用者（音频流开流、前台流程的曲名与窗口行）用它代替分开取代次、条数、路径、定位符的做法，
 *        中途重扫或拔卡不会让它们读到半新半旧的内容。
 */
bool catalog_read_track(uint16_t position, Catalog_TrackTypeDef *out)
{
    bool found = false;

    if (out == NULL)
    {
        return false;
    }

    out->Generation = 0U;
    out->Count = 0U;
    out->Path[0] = '\0';
    (void)memset(&out->Locator, 0, sizeof(out->Locator));

    catalog_lock_enter();
    out->Generation = catalog_generation_locked();
    if (out->Generation != 0U)
    {
        out->Count = catalog_published.Count;
        if (position < catalog_published.Count)
        {
            const uint16_t catalog_index = catalog_sheet_index(position);

            if ((catalog_index < catalog_published.Count) &&
                ((MusicCatalogIndex[catalog_index].Length + 1U) <= sizeof(out->Path)))
            {
                (void)memcpy(out->Path,
                             &MusicCatalogPool.Path[MusicCatalogIndex[catalog_index].Offset],
                             MusicCatalogIndex[catalog_index].Length + 1U);
                out->Locator = MusicCatalogLocator[catalog_index];
                found = true;
            }
        }
    }
    catalog_lock_leave();
    return found;
}
