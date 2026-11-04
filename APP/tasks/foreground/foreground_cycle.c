/**
  ******************************************************************************
  * @file    foreground_cycle.c
  * @brief   表锁最坏延迟的负载实现（spike #199）。
  ******************************************************************************
  */

#include "APP/tasks/foreground/foreground_cycle.h"

#include <stdio.h>
#include <string.h>

#include "APP/tasks/foreground/foreground_clock.h"
#include "APP/tasks/foreground/foreground_load.h"
#include "APP/tasks/foreground/foreground_report.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_service.h"

/** @brief Flash 卷上的测试目录与文件：先写到 tmp，改名成 bin，再删除。 */
#define FOREGROUND_CYCLE_DIR "Spike"
#define FOREGROUND_CYCLE_TMP_PATH "Spike/cycle.tmp"
#define FOREGROUND_CYCLE_BIN_PATH "Spike/cycle.bin"
#define FOREGROUND_CYCLE_STALE_PATH "Spike/load.bin"
/** @brief 每次写 64 KiB（和 P3 一致）。 */
#define FOREGROUND_CYCLE_CHUNK_BYTES 65536U
/** @brief 出错后退避，也是循环之间的间隔。 */
#define FOREGROUND_CYCLE_ERROR_BACKOFF_MS 500U
#define FOREGROUND_CYCLE_NEXT_PACE_MS 200U
/** @brief FTL 回收水位占预留块的百分比（复制自 foreground_load.c，只用于日志里推算水位）。 */
#define FOREGROUND_CYCLE_GC_START_PERCENT 75U

/** @brief 大目录卡：SD 上的目录，条目数达到这个值才算大目录。 */
#define FOREGROUND_OPENLAST_DIR "Music"
#define FOREGROUND_OPENLAST_MIN_ENTRIES 5000U
/** @brief 重复打开的次数，次与次之间让出的毫秒数。 */
#define FOREGROUND_OPENLAST_REPEATS 5U
#define FOREGROUND_OPENLAST_PACE_MS 1000U
/** @brief 扫目录时每读这么多项让出一次 CPU，别饿死同级的 GUI。 */
#define FOREGROUND_OPENLAST_YIELD_EVERY 64U

/* 壁纸资源由 Resource Service 在任务启动前放进 SDRAM，运行期只读；这里只把它当作 SDRAM 里的合成数据源。 */
extern const uint8_t __external_resource_wallpaper_start__[];

typedef enum
{
    FOREGROUND_CYCLE_PREPARE = 0U,
    FOREGROUND_CYCLE_CREATE,
    FOREGROUND_CYCLE_WRITE,
    FOREGROUND_CYCLE_SYNC,
    FOREGROUND_CYCLE_CLOSE,
    FOREGROUND_CYCLE_RENAME,
    FOREGROUND_CYCLE_DELETE,
    FOREGROUND_CYCLE_NEXT
} ForegroundCycleStateTypeDef;

static ForegroundCycleStatsTypeDef foreground_cycle_stats_data;
static ForegroundCycleStateTypeDef foreground_cycle_state;
static Service_Filesystem_FileHandleTypeDef foreground_cycle_file;
static bool foreground_cycle_file_open;
static uint32_t foreground_cycle_written;
static uint32_t foreground_cycle_chunk_index;
static uint32_t foreground_cycle_write_cap_ms;
static bool foreground_cycle_single;
static bool foreground_cycle_finished_flag;
static bool foreground_cycle_partial_now;
static TickType_t foreground_cycle_write_start_tick;
static ForegroundClockStampTypeDef foreground_cycle_write_start;

static ForegroundOpenLastStatsTypeDef foreground_openlast_stats_data;
static bool foreground_openlast_active;
static uint32_t foreground_openlast_done;
static char foreground_openlast_path[SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U];
static Service_Filesystem_DirectoryEntryTypeDef foreground_openlast_entry;

/**
 * @brief 清零 Flash 循环的统计。
 */
void foreground_cycle_reset(void)
{
    memset(&foreground_cycle_stats_data, 0, sizeof(foreground_cycle_stats_data));
    foreground_cycle_finished_flag = false;
}

/**
 * @brief 读 Flash 循环的统计。
 * @return 统计指针；只有前台任务读写。
 */
const ForegroundCycleStatsTypeDef *foreground_cycle_stats(void)
{
    return &foreground_cycle_stats_data;
}

/**
 * @brief 记下阶段开始时的块计数，并按 FTL 公式推算回收水位。
 * @param[out] detail 写入 PHASE start 行的补充信息。
 * @param[in] detail_size detail 的字节数。
 * @note  块计数读取失败时只给出提示，循环仍照常进行。
 */
static void foreground_cycle_begin_blocks(char *detail, size_t detail_size)
{
    Service_Filesystem_SpikeFlashBlocksTypeDef blocks;
    ForegroundCycleStatsTypeDef *stats = &foreground_cycle_stats_data;

    if (!Service_Filesystem_SpikeFlashBlocks(&blocks))
    {
        (void)snprintf(detail, detail_size, "bytes=%lu blocks=unavailable", (unsigned long)stats->TargetBytes);
        return;
    }
    stats->BlocksValid = true;
    stats->BlocksStart = blocks;
    stats->BlocksNow = blocks;
    stats->GcLow = (blocks.ReserveBlocks * FOREGROUND_CYCLE_GC_START_PERCENT + 99U) / 100U;
    (void)snprintf(detail, detail_size, "bytes=%lu free=%lu stale=%lu low=%lu",
                   (unsigned long)stats->TargetBytes, (unsigned long)blocks.FreeBlocks,
                   (unsigned long)blocks.StaleBlocks, (unsigned long)stats->GcLow);
}

/**
 * @brief 开始一个 Flash 循环阶段。
 * @param[in] target_bytes 每个文件的目标字节数，64 KiB 的整数倍。
 * @param[in] write_cap_ms 单个文件写入时限；超过就按已写的字节数收尾，0 表示不限。
 * @param[out] detail 能开始时写入补充信息，不能开始时写入原因。
 * @param[in] detail_size detail 的字节数。
 * @return true 可以开始；false 本阶段“未测”。
 * @note  一个文件写完并删除就算一个循环；目标为 16 MiB 时只做一个循环（`single`），1 MiB 则一直循环到阶段时限。
 */
bool foreground_cycle_begin(uint32_t target_bytes, uint32_t write_cap_ms, char *detail, size_t detail_size)
{
    Service_StatusTypeDef status;

    if (foreground_openlast_stats_data.Found)
    {
        (void)snprintf(detail, detail_size, "bigdir card run: only the path-open phase runs");
        return false;
    }

    status = Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_CYCLE_DIR);
    if ((status != SERVICE_OK) && (status != SERVICE_BUSY))
    {
        (void)snprintf(detail, detail_size, "flash volume status=%s", foreground_status_name((uint32_t)status));
        return false;
    }

    foreground_cycle_stats_data.TargetBytes = target_bytes;
    foreground_cycle_write_cap_ms = write_cap_ms;
    foreground_cycle_single = (target_bytes > (4U * 1024U * 1024U));
    foreground_cycle_finished_flag = false;
    foreground_cycle_file_open = false;
    foreground_cycle_chunk_index = 0U;
    foreground_cycle_state = FOREGROUND_CYCLE_PREPARE;
    foreground_cycle_begin_blocks(detail, detail_size);
    return true;
}

/**
 * @brief 关掉还开着的文件并删掉两个测试文件（忽略失败）。
 */
static void foreground_cycle_cleanup(void)
{
    if (foreground_cycle_file_open)
    {
        (void)Service_Filesystem_CloseFile(foreground_cycle_file);
        foreground_cycle_file_open = false;
    }
    (void)Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_CYCLE_TMP_PATH);
    (void)Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_CYCLE_BIN_PATH);
}

/**
 * @brief 结束一个 Flash 循环阶段：收尾文件。
 */
void foreground_cycle_end(void)
{
    foreground_cycle_cleanup();
}

/**
 * @brief 出错后的善后：记一次错误、打一行日志、收尾文件并退避，回到准备态。
 * @param[in] stage 出错的步骤名。
 * @param[in] status 出错的 Service 状态。
 */
static void foreground_cycle_fail(const char *stage, Service_StatusTypeDef status)
{
    foreground_cycle_stats_data.Errors++;
    foreground_logf("CYCLE error stage=%s status=%s", stage, foreground_status_name((uint32_t)status));
    foreground_cycle_cleanup();
    foreground_cycle_state = FOREGROUND_CYCLE_PREPARE;
    vTaskDelay(pdMS_TO_TICKS(FOREGROUND_CYCLE_ERROR_BACKOFF_MS));
}

/**
 * @brief 准备态：删掉上一轮或 P3 留下的文件。
 */
static void foreground_cycle_prepare(void)
{
    (void)Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_CYCLE_TMP_PATH);
    (void)Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_CYCLE_BIN_PATH);
    (void)Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_CYCLE_STALE_PATH);
    foreground_cycle_state = FOREGROUND_CYCLE_CREATE;
}

/**
 * @brief 新建文件（CREATE_NEW），记墙钟耗时。
 */
static void foreground_cycle_create(void)
{
    const ForegroundClockStampTypeDef start = foreground_clock_now();
    const Service_StatusTypeDef status = Service_Filesystem_OpenFile(
        SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_CYCLE_TMP_PATH, SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
        &foreground_cycle_file);

    if (status != SERVICE_OK)
    {
        foreground_cycle_fail("create", status);
        return;
    }
    foreground_stat_add(&foreground_cycle_stats_data.Create, foreground_clock_elapsed_us(&start));
    foreground_cycle_file_open = true;
    foreground_cycle_written = 0U;
    foreground_cycle_partial_now = false;
    foreground_cycle_write_start = foreground_clock_now();
    foreground_cycle_write_start_tick = xTaskGetTickCount();
    foreground_cycle_state = FOREGROUND_CYCLE_WRITE;
}

/**
 * @brief 写一个 64 KiB 块；写满目标、空间不足或到写入时限就转入同步。
 */
static void foreground_cycle_write(void)
{
    const uint8_t *source = &__external_resource_wallpaper_start__[(foreground_cycle_chunk_index % 3U) *
                                                                   FOREGROUND_CYCLE_CHUNK_BYTES];
    ForegroundCycleStatsTypeDef *stats = &foreground_cycle_stats_data;
    const ForegroundClockStampTypeDef start = foreground_clock_now();
    uint32_t transferred = 0U;
    Service_StatusTypeDef status = Service_Filesystem_WriteFile(foreground_cycle_file, source,
                                                                FOREGROUND_CYCLE_CHUNK_BYTES, &transferred);

    if ((status == SERVICE_OK) && (transferred != FOREGROUND_CYCLE_CHUNK_BYTES))
    {
        status = SERVICE_NO_SPACE;
    }
    if (status == SERVICE_NO_SPACE)
    {
        /* 空间不足：按已写的字节数收尾，这是一个部分循环。 */
        foreground_cycle_written += transferred;
        foreground_cycle_partial_now = true;
        foreground_stat_add(&stats->WriteTotal, foreground_clock_elapsed_us(&foreground_cycle_write_start));
        foreground_cycle_state = FOREGROUND_CYCLE_SYNC;
        return;
    }
    if (status != SERVICE_OK)
    {
        foreground_cycle_fail("write", status);
        return;
    }
    foreground_stat_add(&stats->WriteChunk, foreground_clock_elapsed_us(&start));

    foreground_cycle_written += transferred;
    foreground_cycle_chunk_index++;
    if (foreground_cycle_written >= stats->TargetBytes)
    {
        foreground_stat_add(&stats->WriteTotal, foreground_clock_elapsed_us(&foreground_cycle_write_start));
        foreground_cycle_state = FOREGROUND_CYCLE_SYNC;
    }
    else if ((foreground_cycle_write_cap_ms != 0U) &&
             ((TickType_t)(xTaskGetTickCount() - foreground_cycle_write_start_tick) >=
              pdMS_TO_TICKS(foreground_cycle_write_cap_ms)))
    {
        foreground_cycle_partial_now = true;
        foreground_stat_add(&stats->WriteTotal, foreground_clock_elapsed_us(&foreground_cycle_write_start));
        foreground_cycle_state = FOREGROUND_CYCLE_SYNC;
    }
    vTaskDelay(pdMS_TO_TICKS(1U));
}

/**
 * @brief 同步、关闭、改名、删除各占一步，记各自的墙钟耗时；删除另记卷锁持有。
 */
static void foreground_cycle_finish_file(void)
{
    ForegroundCycleStatsTypeDef *stats = &foreground_cycle_stats_data;
    const ForegroundClockStampTypeDef start = foreground_clock_now();
    Service_StatusTypeDef status;

    switch (foreground_cycle_state)
    {
        case FOREGROUND_CYCLE_SYNC:
            status = Service_Filesystem_SyncFile(foreground_cycle_file);
            if (status != SERVICE_OK)
            {
                foreground_cycle_fail("sync", status);
                return;
            }
            foreground_stat_add(&stats->Sync, foreground_clock_elapsed_us(&start));
            foreground_cycle_state = FOREGROUND_CYCLE_CLOSE;
            break;

        case FOREGROUND_CYCLE_CLOSE:
            status = Service_Filesystem_CloseFile(foreground_cycle_file);
            if (status != SERVICE_OK)
            {
                foreground_cycle_fail("close", status);
                return;
            }
            foreground_cycle_file_open = false;
            foreground_stat_add(&stats->Close, foreground_clock_elapsed_us(&start));
            foreground_cycle_state = FOREGROUND_CYCLE_RENAME;
            break;

        case FOREGROUND_CYCLE_RENAME:
            status = Service_Filesystem_RenameFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_CYCLE_TMP_PATH,
                                                   FOREGROUND_CYCLE_BIN_PATH);
            if (status != SERVICE_OK)
            {
                foreground_cycle_fail("rename", status);
                return;
            }
            foreground_stat_add(&stats->Rename, foreground_clock_elapsed_us(&start));
            foreground_cycle_state = FOREGROUND_CYCLE_DELETE;
            break;

        default:
            (void)Service_Filesystem_SpikeLastHoldUs();
            status = Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_CYCLE_BIN_PATH);
            if (status != SERVICE_OK)
            {
                foreground_cycle_fail("delete", status);
                return;
            }
            foreground_stat_add(&stats->Delete, foreground_clock_elapsed_us(&start));
            foreground_stat_add(&stats->DeleteHold, Service_Filesystem_SpikeLastHoldUs());
            stats->LastBytes = foreground_cycle_written;
            stats->Cycles++;
            if (foreground_cycle_partial_now)
            {
                stats->PartialCycles++;
            }
            foreground_cycle_state = FOREGROUND_CYCLE_NEXT;
            break;
    }
}

/**
 * @brief 循环负载的一步：至多一次 64 KiB 写，或同步、关闭、改名、删除中的一个。
 */
void foreground_cycle_step(void)
{
    switch (foreground_cycle_state)
    {
        case FOREGROUND_CYCLE_PREPARE:
            foreground_cycle_prepare();
            break;
        case FOREGROUND_CYCLE_CREATE:
            foreground_cycle_create();
            break;
        case FOREGROUND_CYCLE_WRITE:
            foreground_cycle_write();
            break;
        case FOREGROUND_CYCLE_NEXT:
            if (foreground_cycle_single)
            {
                foreground_cycle_finished_flag = true;
            }
            else
            {
                foreground_cycle_state = FOREGROUND_CYCLE_PREPARE;
            }
            vTaskDelay(pdMS_TO_TICKS(FOREGROUND_CYCLE_NEXT_PACE_MS));
            break;
        default:
            foreground_cycle_finish_file();
            break;
    }
}

/**
 * @brief 阶段的目标是否已达到。
 * @return true 为一次性的大文件循环已删除完成；1 MiB 循环恒为 false，由阶段时限结束。
 */
bool foreground_cycle_finished(void)
{
    return foreground_cycle_finished_flag;
}

/**
 * @brief 汇总前重新读一次块计数。
 */
void foreground_cycle_refresh(void)
{
    Service_Filesystem_SpikeFlashBlocksTypeDef blocks;

    if (foreground_cycle_stats_data.BlocksValid && Service_Filesystem_SpikeFlashBlocks(&blocks))
    {
        foreground_cycle_stats_data.BlocksNow = blocks;
    }
}

/**
 * @brief 清零按路径打开的统计（保留“已找到大目录”的标记，让后面的 Flash 阶段知道这一遍只跑它）。
 */
void foreground_openlast_reset(void)
{
    const bool found = foreground_openlast_stats_data.Found;
    const uint32_t entries = foreground_openlast_stats_data.Entries;
    const uint32_t scan_us = foreground_openlast_stats_data.ScanUs;
    char last[sizeof(foreground_openlast_stats_data.Last)];

    memcpy(last, foreground_openlast_stats_data.Last, sizeof(last));
    memset(&foreground_openlast_stats_data, 0, sizeof(foreground_openlast_stats_data));
    foreground_openlast_stats_data.Found = found;
    foreground_openlast_stats_data.Entries = entries;
    foreground_openlast_stats_data.ScanUs = scan_us;
    memcpy(foreground_openlast_stats_data.Last, last, sizeof(last));
}

/**
 * @brief 读按路径打开的统计。
 * @return 统计指针；只有前台任务读写。
 */
const ForegroundOpenLastStatsTypeDef *foreground_openlast_stats(void)
{
    return &foreground_openlast_stats_data;
}

/**
 * @brief 扫一遍大目录，找到最后一个文件。
 * @param[out] status 出错时的 Service 状态。
 * @return true 扫完；false 目录打不开或读失败。
 */
static bool foreground_openlast_scan(Service_StatusTypeDef *status)
{
    ForegroundOpenLastStatsTypeDef *stats = &foreground_openlast_stats_data;
    Service_Filesystem_DirectoryHandleTypeDef directory;
    const ForegroundClockStampTypeDef start = foreground_clock_now();
    uint32_t visited = 0U;

    *status = Service_Filesystem_OpenDirectory(SERVICE_FILESYSTEM_VOLUME_SD, FOREGROUND_OPENLAST_DIR, &directory);
    if (*status != SERVICE_OK)
    {
        return false;
    }

    for (;;)
    {
        *status = Service_Filesystem_ReadDirectory(directory, &foreground_openlast_entry);
        if ((*status != SERVICE_OK) || (foreground_openlast_entry.Name[0] == '\0'))
        {
            break;
        }
        if (!foreground_openlast_entry.IsDirectory)
        {
            stats->Entries++;
            (void)snprintf(foreground_openlast_path, sizeof(foreground_openlast_path), "%s/%.240s",
                           FOREGROUND_OPENLAST_DIR, foreground_openlast_entry.Name);
        }
        visited++;
        if ((visited % FOREGROUND_OPENLAST_YIELD_EVERY) == 0U)
        {
            taskYIELD();
        }
    }
    stats->ScanUs = foreground_clock_elapsed_us(&start);
    (void)Service_Filesystem_CloseDirectory(directory);
    return (*status == SERVICE_OK);
}

/**
 * @brief 开始“按路径打开最后一首”阶段：扫目录，条目够多才算大目录卡。
 * @param[out] detail 能开始时写入目录与文件名，不能开始时写入原因。
 * @param[in] detail_size detail 的字节数。
 * @return true 可以开始；false 本阶段“未测”（没有大目录卡）。
 */
bool foreground_openlast_begin(char *detail, size_t detail_size)
{
    ForegroundOpenLastStatsTypeDef *stats = &foreground_openlast_stats_data;
    Service_StatusTypeDef status = SERVICE_OK;
    const char *name;

    stats->Found = false;
    stats->Entries = 0U;
    foreground_openlast_path[0] = '\0';

    if (!foreground_openlast_scan(&status))
    {
        (void)snprintf(detail, detail_size, "%s scan status=%s", FOREGROUND_OPENLAST_DIR,
                       foreground_status_name((uint32_t)status));
        return false;
    }
    if (stats->Entries < FOREGROUND_OPENLAST_MIN_ENTRIES)
    {
        (void)snprintf(detail, detail_size, "%s entries=%lu (<%lu, not the bigdir card)", FOREGROUND_OPENLAST_DIR,
                       (unsigned long)stats->Entries, (unsigned long)FOREGROUND_OPENLAST_MIN_ENTRIES);
        return false;
    }

    name = strrchr(foreground_openlast_path, '/');
    name = (name != NULL) ? (name + 1) : foreground_openlast_path;
    (void)snprintf(stats->Last, sizeof(stats->Last), "%.47s", name);
    stats->Found = true;
    foreground_openlast_active = true;
    foreground_openlast_done = 0U;
    (void)snprintf(detail, detail_size, "dir=%s entries=%lu scan=%luus last=%s", FOREGROUND_OPENLAST_DIR,
                   (unsigned long)stats->Entries, (unsigned long)stats->ScanUs, stats->Last);
    return true;
}

/**
 * @brief 结束“按路径打开最后一首”阶段。
 */
void foreground_openlast_end(void)
{
    foreground_openlast_active = false;
}

/**
 * @brief 一步：按路径打开最后一个文件、关闭它，记墙钟与 SD 卷锁持有。
 */
void foreground_openlast_step(void)
{
    ForegroundOpenLastStatsTypeDef *stats = &foreground_openlast_stats_data;
    Service_Filesystem_FileHandleTypeDef file;
    const ForegroundClockStampTypeDef start = foreground_clock_now();
    Service_StatusTypeDef status;

    if (foreground_openlast_done >= FOREGROUND_OPENLAST_REPEATS)
    {
        vTaskDelay(pdMS_TO_TICKS(50U));
        return;
    }

    (void)Service_Filesystem_SpikeLastHoldUs();
    status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_SD, foreground_openlast_path,
                                         SERVICE_FILESYSTEM_FILE_MODE_READ, &file);
    if (status != SERVICE_OK)
    {
        stats->Errors++;
        stats->LastStatus = (uint32_t)status;
        foreground_openlast_done++;
        vTaskDelay(pdMS_TO_TICKS(FOREGROUND_OPENLAST_PACE_MS));
        return;
    }
    foreground_stat_add(&stats->Open, foreground_clock_elapsed_us(&start));
    foreground_stat_add(&stats->OpenHold, Service_Filesystem_SpikeLastHoldUs());

    {
        const ForegroundClockStampTypeDef close_start = foreground_clock_now();

        status = Service_Filesystem_CloseFile(file);
        if (status != SERVICE_OK)
        {
            stats->Errors++;
            stats->LastStatus = (uint32_t)status;
        }
        else
        {
            foreground_stat_add(&stats->Close, foreground_clock_elapsed_us(&close_start));
        }
    }

    foreground_openlast_done++;
    vTaskDelay(pdMS_TO_TICKS(FOREGROUND_OPENLAST_PACE_MS));
}

/**
 * @brief 阶段的目标是否已达到。
 * @return true 为已重复打开了规定次数。
 */
bool foreground_openlast_finished(void)
{
    return foreground_openlast_active && (foreground_openlast_done >= FOREGROUND_OPENLAST_REPEATS);
}
