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
 *          扫描是后台任务里的单步作业（`catalog_scan_step()`，音乐注册进注册表的 `background_step`，ARC-22）：一步最多一次
 *          目录接口调用，读目录一步读一条、每条放一次锁；遍历状态（目录栈、当前路径、目录句柄、计数）放静态存储
 *          `.apps_sdram`（ARC-28），不占后台任务的 6 KiB 栈。每步先比对 SD 卷序号：变了就关目录（返回值不看）并丢弃，
 *          卡还在就重新开始；扫描只在卷序号变化后发生（后台任务订阅了卷事件）。发布成功后叫醒前台任务一次
 *          （`foreground_task_wake()`，APP 内部接口），前台流程据此重新处理最新需求。
 */

#include "catalog.h"
#include "catalog_name.h"
#include "catalog_sheet.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "APP/tasks/background/background_task_config.h"
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

/** @brief 已发布的状态；只在持曲库锁时访问。 */
static Catalog_PublishedTypeDef catalog_published;

/** @brief 上一次发布用的代次；只在发布时加 1（持锁），所以一次开机内不重复。位于 .bss，上电为零。 */
static uint32_t catalog_generation_counter;

/** @brief 曲库锁（静态互斥量）；`catalog_boot_init()` 创建。 */
static StaticSemaphore_t catalog_lock_storage;
static SemaphoreHandle_t catalog_lock;

static Catalog_MusicTypeDef MusicCatalogPool
    __attribute__((section(".storage_catalog"), aligned(32)));

static Catalog_MusicItemsTypeDef MusicCatalogIndex[CATALOG_MUSIC_MAX_NUM]
    __attribute__((section(".storage_catalog"), aligned(32)));

/** @brief 与 MusicCatalogIndex 同下标的文件定位符（16 B/条）；扫描时随条目写入，只在同一曲库代次内有效。 */
static Service_Filesystem_FileLocatorTypeDef MusicCatalogLocator[CATALOG_MUSIC_MAX_NUM]
    __attribute__((section(".storage_catalog"), aligned(32)));

/** @brief 扫描单步状态机的步骤：下一步要做哪一次目录调用。 */
typedef enum
{
    CATALOG_SCAN_IDLE = 0U, /**< 没有进行中的扫描：卷没挂，或这一轮已发布、已放弃；等卷序号变化。 */
    CATALOG_SCAN_OPEN,      /**< 下一步：打开 `Current` 目录。 */
    CATALOG_SCAN_READ,      /**< 下一步：读一条目录项。 */
    CATALOG_SCAN_CLOSE      /**< 下一步：关目录（读到结束标记、表满，或读失败之后）。 */
} Catalog_ScanPhaseTypeDef;

/** @brief 本轮扫描关目录之后要怎样收尾。 */
typedef enum
{
    CATALOG_SCAN_FAILURE_NONE = 0U, /**< 没有失败：继续下一个目录，或发布。 */
    CATALOG_SCAN_FAILURE_DISCARD,   /**< 卷不可用：丢弃，不发布；卡被拔走就等卷事件，卡还在读不了就记 ERROR 要求重插卡。 */
    CATALOG_SCAN_FAILURE_ERROR      /**< 读或关目录的其他错误：不发布，记 ERROR，等下一次插卡。 */
} Catalog_ScanFailureTypeDef;

/**
 * @brief 扫描的遍历状态：全部静态，放 `.apps_sdram`（NOLOAD，不自动清零，`catalog_boot_init()` 整块写入，HWD-2）。
 * @note  只在后台任务里访问（单写者）。约 33 KiB，其中目录栈 128 层 × 256 B；曲库本身（路径池、条目、定位符）在 `.storage_catalog`。
 */
typedef struct
{
    Catalog_ScanPhaseTypeDef Phase;
    Catalog_ScanFailureTypeDef Failure;
    uint32_t SeenSequence;      /**< 最近处理过的 SD 卷序号；扫描进行中也是本轮扫描开始时的序号，发布时与当时的卷序号比对。 */
    uint32_t Index;             /**< 本轮已收录的条数（播放列表的工作下标）。 */
    uint32_t StackTop;          /**< 待扫目录栈的深度。 */
    bool DirectoryOpen;         /**< `Directory` 句柄是否有效（已打开、还没关）。 */
    bool OpenRetried;           /**< 本目录“卷不可用时再开一次”已经用掉。 */
    bool CurrentIsRoot;         /**< `Current` 是根目录 `Music`。 */
    uint32_t ClockLast;         /**< 周期计数器上次读数。 */
    uint64_t ClockCycles;       /**< 本轮累计周期数（64 位）：DWT 为 32 位，480 MHz 下约 9 s 回绕，扫描可能更长，所以边走边累加。 */
    Service_Filesystem_DirectoryHandleTypeDef Directory;
    Service_Filesystem_DirectoryEntryTypeDef Entry;
    char Current[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];
    char Child[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];
    char Stack[CATALOG_DIR_STACK_MAX][SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];
} Catalog_ScanStateTypeDef;

static Catalog_ScanStateTypeDef catalog_scan_state
    __attribute__((section(CATALOG_SDRAM_SECTION), aligned(32)));

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
 * @note  只在 Create Task（Boot）里调用一次，早于创建任何会读写曲库的任务（音频读取、前台、后台），
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
    (void)memset(&catalog_scan_state, 0, sizeof(catalog_scan_state)); /* NOLOAD 段不自动清零：第一次使用前整块写入 */
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
    catalog_scan_state.ClockLast = CortexM7CycleCounter_Read();
    catalog_scan_state.ClockCycles = 0U;
}

/**
 * @brief 把距上次读数过去的周期数累加进本轮扫描耗时。
 * @note  扫描进行中的每一步开头、每次收尾记日志之前调用；相邻两次调用相隔一步（毫秒级，BUSY 重试也只有约 10 ms，
 *        后台任务被抢占也远小于 32 位计数器的回绕周期），所以无符号减法取差值总是对的。含目录读取、打开、等待与任务切换，
 *        也就是从开始到结束的总时间（第 4、5 次上板的开机扫描数字同一口径）。
 */
static void catalog_scan_clock_poll(void)
{
    const uint32_t now = CortexM7CycleCounter_Read();

    catalog_scan_state.ClockCycles += (uint32_t)(now - catalog_scan_state.ClockLast);
    catalog_scan_state.ClockLast = now;
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

    us = (catalog_scan_state.ClockCycles * 1000000ULL) / frequency_hz;
    return (us > UINT32_MAX) ? UINT32_MAX : (uint32_t)us;
}

/**
 * @brief 记扫描结束一行 INFO：结果、条数、耗时（DIAG-6）。
 * @param[in] result 结果名，静态字符串：ok、dfs_error、aborted。
 * @note  常开，每次扫描一行；文本只含结果名与数值。耗时单位微秒，含目录读取、打开、等待与扫描中的任务切换，不含建播放列表。
 *        条数取工作下标：丢弃本次扫描时调用者已把它清零。
 */
static void catalog_scan_log_end(const char *result)
{
    char text[64];

    catalog_scan_clock_poll();
    (void)snprintf(text, sizeof(text), "Scan done res=%s n=%lu us=%lu",
                   result,
                   (unsigned long)catalog_scan_state.Index,
                   (unsigned long)catalog_scan_clock_us());
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, catalog_log_tag, text);
}

/**
 * @brief 拔卡或卸载时作废已发布的曲库：锁内把代次和条数置 0。
 * @note  不清表，不动代次计数（下一次发布仍加 1，不会重复）。即使不调用它，取代次时卷序号不符也会算 0，
 *        所以这是让已发布的状态尽快归零，不是正确性所必需。
 */
static void catalog_published_invalidate(void)
{
    catalog_lock_enter();
    catalog_published.Generation = 0U;
    catalog_published.Count = 0U;
    catalog_lock_leave();
}

/**
 * @brief 开始一轮扫描：锁内把代次置 0、条数置 0，记下此刻的卷序号，清遍历状态，根目录 `Music` 作为第一个要打开的目录。
 * @param[in] sequence 这一轮对应的 SD 卷序号（调用者刚读到的）。
 * @note  此后读者看到代次 0，不碰表；扫描者写表不拿锁。整个状态机里持锁的只有这里和发布两处。
 */
static void catalog_scan_begin(uint32_t sequence)
{
    catalog_scan_state.SeenSequence = sequence;

    catalog_lock_enter();
    catalog_published.Generation = 0U;
    catalog_published.Count = 0U;
    catalog_lock_leave();

    catalog_scan_state.Index = 0U;
    MusicCatalogPool.Tail = 0U;
    catalog_scan_state.Failure = CATALOG_SCAN_FAILURE_NONE;
    catalog_scan_state.DirectoryOpen = false;
    catalog_scan_state.OpenRetried = false;
    catalog_scan_clock_start();

    (void)memset(catalog_scan_state.Current, 0, sizeof(catalog_scan_state.Current));
    (void)memcpy(catalog_scan_state.Current, "Music", sizeof("Music"));
    catalog_scan_state.CurrentIsRoot = true;
    catalog_scan_state.StackTop = 0U;
    catalog_scan_state.Phase = CATALOG_SCAN_OPEN;
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

        if (volume.Mounted && (volume.Sequence == catalog_scan_state.SeenSequence))
        {
            catalog_generation_counter++;
            if (catalog_generation_counter == 0U)
            {
                catalog_generation_counter = 1U;
            }
            catalog_published.Generation = catalog_generation_counter;
            catalog_published.Count = (uint16_t)catalog_scan_state.Index;
            catalog_published.VolumeSequence = catalog_scan_state.SeenSequence;
            published = true;
        }
    }
    catalog_lock_leave();
    return published;
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

    if (catalog_scan_state.Index >= CATALOG_MUSIC_MAX_NUM)
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
    MusicCatalogIndex[catalog_scan_state.Index].Offset = MusicCatalogPool.Tail;
    MusicCatalogIndex[catalog_scan_state.Index].Length = length;
    MusicCatalogLocator[catalog_scan_state.Index] = *locator;
    MusicCatalogPool.Tail += needed;
    catalog_scan_state.Index++;
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
 * @brief 扫描做完：建播放列表，按发布规则发布，叫醒前台任务一次。
 * @note  发布时锁内比对卷序号：卷在扫描途中变了（或卷已不在），这份结果属于旧卡，不发布、记 aborted；
 *        卡还在的话下一步看到新的卷序号会重新开始。发布成功后在放锁之后叫醒前台任务（GUI 不会重写没变的需求，
 *        前台要主动重新处理最新需求：曲名答过代次 0 的、窗口格里还是空窗的）；被丢弃或失败的扫描不发布，也不叫醒。
 */
static void catalog_scan_finish_ok(void)
{
    catalog_sheet_fill((uint16_t)catalog_scan_state.Index);
    if (!catalog_scan_publish())
    {
        catalog_scan_state.Index = 0U;
        catalog_scan_log_end("aborted");
    }
    else
    {
        catalog_scan_log_end("ok");
        foreground_task_wake();
    }
    catalog_scan_state.Phase = CATALOG_SCAN_IDLE;
}

/**
 * @brief 本轮因为卷不可用（NO_MEDIA、NOT_READY、INVALID_HANDLE）被丢弃：不发布，等卷事件。
 * @note  收尾时再看一眼卷：序号变了或卷不在是卡没了（或换了），卷事件已经或即将叫醒后台任务，这里不报错；
 *        序号没变且卷仍挂着，是卡还在、读不了（设备出错），没有边沿就没有卷事件，等也等不来，所以记一条 ERROR
 *        要求重插卡，曲库保持代次 0，之后不再自己重试，到卷序号变化为止（#211）。
 */
static void catalog_scan_finish_discard(void)
{
    Volume_StatusTypeDef volume;

    catalog_scan_state.Index = 0U;
    catalog_scan_log_end("aborted");
    catalog_scan_state.Phase = CATALOG_SCAN_IDLE;

    volume = volume_status(VOLUME_ID_SD);
    if (volume.Mounted && (volume.Sequence == catalog_scan_state.SeenSequence))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               catalog_log_tag,
                               "Catalog scan aborted but the card is still there; reinsert the card.");
    }
}

/**
 * @brief 读或关目录的其他错误：不发布，记 ERROR，等下一次插卡（沿用 STOR-7）。
 */
static void catalog_scan_finish_error(void)
{
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                           catalog_log_tag,
                           "Music catalog dfs failed.");
    catalog_scan_log_end("dfs_error");
    catalog_scan_state.Phase = CATALOG_SCAN_IDLE;
}

/**
 * @brief 弹出栈顶目录作为下一个要打开的目录；栈空就扫描结束，走发布。
 * @return 恒为 0：状态变了，马上再调一步。
 */
static uint32_t catalog_scan_next_directory(void)
{
    if (catalog_scan_state.StackTop == 0U)
    {
        catalog_scan_finish_ok();
        return 0U;
    }

    catalog_scan_state.StackTop--;
    (void)memcpy(catalog_scan_state.Current,
                 catalog_scan_state.Stack[catalog_scan_state.StackTop],
                 sizeof(catalog_scan_state.Current));
    catalog_scan_state.CurrentIsRoot = false;
    catalog_scan_state.OpenRetried = false;
    catalog_scan_state.Phase = CATALOG_SCAN_OPEN;
    return 0U;
}

/**
 * @brief 卷序号变了（或第一次看到一个序号）：旧的全部作废，再看现状。
 * @param[in] volume 这一步开头读到的 SD 卷状态。
 * @return 卷不在返回 `BACKGROUND_WAIT_FOREVER`（等卷事件）；卷挂着返回 0（下一步开始打开根目录）。
 * @note  正在扫描就丢弃：目录开着先关（返回值不看，卡可能已经没了），记 `aborted`；之后卷不在就作废已发布的曲库并等卷事件，
 *        卷还挂着（换卡，或被吞的拔出加重新挂载）就从头重新开始。这一步最多一次目录调用（关目录），不在同一步里再打开。
 */
static uint32_t catalog_scan_restart(Volume_StatusTypeDef volume)
{
    if (catalog_scan_state.DirectoryOpen)
    {
        (void)Service_Filesystem_CloseDirectory(catalog_scan_state.Directory);
        catalog_scan_state.DirectoryOpen = false;
    }

    if (catalog_scan_state.Phase != CATALOG_SCAN_IDLE)
    {
        catalog_scan_state.Index = 0U;
        catalog_scan_log_end("aborted");
    }
    catalog_scan_state.Phase = CATALOG_SCAN_IDLE;

    if (!volume.Mounted)
    {
        catalog_scan_state.SeenSequence = volume.Sequence;
        catalog_published_invalidate();
        return BACKGROUND_WAIT_FOREVER;
    }

    catalog_scan_begin(volume.Sequence);
    return 0U;
}

/**
 * @brief 打开 `Current` 目录（本步唯一的文件调用）。
 * @return 多少毫秒后再叫它。
 * @note  `SERVICE_BUSY`（槽用尽）不算失败：约 10 ms 后再试，不在步里等。打开得到卷不可用时，第一次失败发生在卷任务对账之前，
 *        所以再开一次（它在 Filesystem 入口等闸，等到本轮对账结束）才下结论：卡没了（再次得到卷不可用，或下一步卷序号已变）
 *        还是卡还在（照常打开）。其他错误：根目录按空库发布并记 WARN（沿用 STOR-7），子目录跳过并记 WARN。
 */
static uint32_t catalog_scan_step_open(void)
{
    const Service_StatusTypeDef status = Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD,
                                                                          catalog_scan_state.Current,
                                                                          &catalog_scan_state.Directory);

    if (status == SERVICE_OK)
    {
        catalog_scan_state.DirectoryOpen = true;
        catalog_scan_state.Phase = CATALOG_SCAN_READ;
        return 0U;
    }

    if (status == SERVICE_BUSY)
    {
        return CATALOG_BUSY_RETRY_MS;
    }

    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        if (!catalog_scan_state.OpenRetried)
        {
            catalog_scan_state.OpenRetried = true;
            return 0U;
        }
        catalog_scan_finish_discard();
        return 0U;
    }

    if (catalog_scan_state.CurrentIsRoot)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                               catalog_log_tag,
                               "Music Directory Open Failed.");
        catalog_scan_finish_ok();
        return 0U;
    }

    (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                           catalog_log_tag,
                           "Sub Directory Open Failed.");
    return catalog_scan_next_directory();
}

/**
 * @brief 读一条目录项并处理它（本步唯一的文件调用，每条放一次锁）。
 * @return 多少毫秒后再叫它。
 * @note  `SERVICE_BUSY` 约 10 ms 后再试。读失败：卷不可用就丢弃（先关目录），其他错误记 ERROR、不发布（先关目录）。
 *        结束标记、表满都转到关目录。点开头的项跳过；子目录入栈（栈满记 WARN 并跳过）；`.mp3` 收录，条数满或池满发布截断结果。
 */
static uint32_t catalog_scan_step_read(void)
{
    const Service_StatusTypeDef status = Service_Filesystem_ReadDirectory(catalog_scan_state.Directory,
                                                                          &catalog_scan_state.Entry);

    if (status == SERVICE_BUSY)
    {
        return CATALOG_BUSY_RETRY_MS;
    }

    if (status != SERVICE_OK)
    {
        if (Service_Filesystem_StatusIsVolumeUnavailable(status))
        {
            catalog_scan_state.Failure = CATALOG_SCAN_FAILURE_DISCARD;
        }
        else
        {
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                   catalog_log_tag,
                                   "Music Directory Read Failed.");
            catalog_scan_state.Failure = CATALOG_SCAN_FAILURE_ERROR;
        }
        catalog_scan_state.Phase = CATALOG_SCAN_CLOSE;
        return 0U;
    }

    if (catalog_scan_state.Entry.Name[0] == '\0')
    {
        catalog_scan_state.Phase = CATALOG_SCAN_CLOSE;
        return 0U;
    }

    if (catalog_scan_state.Entry.Name[0] == '.')
    {
        return 0U;
    }

    if (!catalog_music_join_path(catalog_scan_state.Child,
                                 sizeof(catalog_scan_state.Child),
                                 catalog_scan_state.Current,
                                 catalog_scan_state.Entry.Name))
    {
        return 0U;
    }

    if (catalog_scan_state.Entry.IsDirectory)
    {
        if (catalog_scan_state.StackTop >= CATALOG_DIR_STACK_MAX)
        {
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN,
                                   catalog_log_tag,
                                   "Music Directory Stack Full.");
            return 0U;
        }

        (void)memcpy(catalog_scan_state.Stack[catalog_scan_state.StackTop],
                     catalog_scan_state.Child,
                     sizeof(catalog_scan_state.Child));
        catalog_scan_state.StackTop++;
        return 0U;
    }

    if (!catalog_music_file_strcheck(catalog_scan_state.Entry.Name))
    {
        return 0U;
    }

    if (!catalog_music_append(catalog_scan_state.Child, &catalog_scan_state.Entry.Locator))
    {
        /* 条目满或池满：发布截断结果，不再扫其余目录。 */
        catalog_scan_state.StackTop = 0U;
        catalog_scan_state.Phase = CATALOG_SCAN_CLOSE;
    }
    return 0U;
}

/**
 * @brief 关目录（本步唯一的文件调用），然后按本轮的收尾方式往下走。
 * @return 多少毫秒后再叫它。
 * @note  `SERVICE_BUSY` 约 10 ms 后再试。关目录得到卷不可用就丢弃（盖过之前的读错误，因为卡没了不是文件错误）；
 *        其他关目录错误不发布，记 ERROR。
 */
static uint32_t catalog_scan_step_close(void)
{
    const Service_StatusTypeDef status = Service_Filesystem_CloseDirectory(catalog_scan_state.Directory);

    if (status == SERVICE_BUSY)
    {
        return CATALOG_BUSY_RETRY_MS;
    }

    catalog_scan_state.DirectoryOpen = false;
    if (status != SERVICE_OK)
    {
        if (Service_Filesystem_StatusIsVolumeUnavailable(status))
        {
            catalog_scan_state.Failure = CATALOG_SCAN_FAILURE_DISCARD;
        }
        else if (catalog_scan_state.Failure == CATALOG_SCAN_FAILURE_NONE)
        {
            catalog_scan_state.Failure = CATALOG_SCAN_FAILURE_ERROR;
        }
    }

    switch (catalog_scan_state.Failure)
    {
    case CATALOG_SCAN_FAILURE_DISCARD:
        catalog_scan_finish_discard();
        return 0U;
    case CATALOG_SCAN_FAILURE_ERROR:
        catalog_scan_finish_error();
        return 0U;
    case CATALOG_SCAN_FAILURE_NONE:
    default:
        return catalog_scan_next_directory();
    }
}

/**
 * @brief 扫描作业单步（音乐注册进注册表的 `background_step`，ARC-22）。
 * @return 多少毫秒后再叫它：0 马上（还有活）；`CATALOG_BUSY_RETRY_MS`（槽用尽，过约 10 ms 重试）；
 *         `BACKGROUND_WAIT_FOREVER`（没有活：卷没挂，或这一轮已发布、已放弃，等卷事件叫醒）。
 * @note  一步最多一次目录接口调用；读目录一步读一条，每条放一次锁；不批量。每步先比对 SD 卷序号：变了就关目录
 *        （返回值不看）并丢弃，卡还在就重新开始，卡不在就作废已发布的曲库。扫描只在卷序号变化后发生，后台任务订阅了卷事件，
 *        所以丢叫醒、合并叫醒、晚订阅都不会漏（任务入口登记后第一圈就调所有作业）。不向 GUI 发布进度。
 *        扫描进行中返回 0，后台任务马上再调一圈（别的作业各调一次）；优先级 1 的后台任务被前台、GUI 抢占时，
 *        持锁的这一步最多是一次目录接口调用的时间。
 */
uint32_t catalog_scan_step(void)
{
    const Volume_StatusTypeDef volume = volume_status(VOLUME_ID_SD);

    if (catalog_scan_state.Phase != CATALOG_SCAN_IDLE)
    {
        catalog_scan_clock_poll();
    }

    if (volume.Sequence != catalog_scan_state.SeenSequence)
    {
        return catalog_scan_restart(volume);
    }

    switch (catalog_scan_state.Phase)
    {
    case CATALOG_SCAN_OPEN:
        return catalog_scan_step_open();
    case CATALOG_SCAN_READ:
        return catalog_scan_step_read();
    case CATALOG_SCAN_CLOSE:
        return catalog_scan_step_close();
    case CATALOG_SCAN_IDLE:
    default:
        return BACKGROUND_WAIT_FOREVER;
    }
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
