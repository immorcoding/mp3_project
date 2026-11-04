/**
  ******************************************************************************
  * @file    foreground_load.c
  * @brief   前台任务各阶段的负载实现（spike #191）。
  ******************************************************************************
  */

#include "APP/tasks/foreground/foreground_load.h"

#include <stdio.h>
#include <string.h>

#include "APP/tasks/foreground/foreground_cycle.h"
#include "APP/tasks/foreground/foreground_report.h"
#include "APP/tasks/foreground/spike_audio_read.h"
#include "APP/tasks/foreground/spike_unplug.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/filesystem/filesystem_directory.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/filesystem_spike.h"

/** @brief SD 卷上的大 TXT，用户手动放入，至少 5 MiB。 */
#define FOREGROUND_TXT_PATH "Spike/big.txt"
#define FOREGROUND_TXT_MIN_BYTES (5U * 1024U * 1024U)
/** @brief 单次读取上限 32 KiB，与 SD 弹跳缓冲一次 DMA 的大小一致。 */
#define FOREGROUND_TXT_CHUNK_BYTES 32768U
/** @brief 固定的每页字符数（按字节算），页数 = 文件大小 / 它。 */
#define FOREGROUND_TXT_PAGE_BYTES 2048U
/** @brief 模拟翻页时在随机页起点读的字节数。 */
#define FOREGROUND_TXT_PAGE_READ_BYTES 4096U
/** @brief 每读这么多个 32 KiB 块，插一次随机翻页。 */
#define FOREGROUND_TXT_PAGE_EVERY 8U
/** @brief 每步之后让出的毫秒数，不做 CPU 重活，也给同级的 GUI 留时间。 */
#define FOREGROUND_TXT_PACE_MS 2U
/** @brief 读失败后重新打开的最小间隔。 */
#define FOREGROUND_TXT_RETRY_MS 500U

/** @brief Flash 卷上的测试目录与文件。 */
#define FOREGROUND_FLASH_DIR "Spike"
#define FOREGROUND_FLASH_PATH "Spike/load.bin"
/** @brief 每次写 64 KiB 并同步。 */
#define FOREGROUND_FLASH_CHUNK_BYTES 65536U
/** @brief 写到约 1 MiB 后删除。 */
#define FOREGROUND_FLASH_TARGET_BYTES (1024U * 1024U)
/**
 * @brief 删除之后进入回收阶段：每次调用都看 FTL 的擦除计数有没有加一，连续空转这么多次就回到写入。
 * @note  板测（#191 第一轮）里回收 96 次全是空转（约 14 µs），因为 FTL 只在空闲块数不高于水位时才擦除
 *        （flash_ftl.c 的 GC 步骤，读代码所得），所以现在先按水位算出要写多少，写到水位以下再回收。
 */
#define FOREGROUND_FLASH_IDLE_RECLAIMS 4U
/** @brief 一个回收阶段最多调用的次数，防止在水位附近一直擦。 */
#define FOREGROUND_FLASH_RECLAIM_STAGE_CAP 64U
/** @brief 写入之间只让出 1 ms：消耗空闲块的速度决定能否在时限内逼近水位。 */
#define FOREGROUND_FLASH_PACE_MS 1U
#define FOREGROUND_FLASH_RECLAIM_PACE_MS 5U
#define FOREGROUND_FLASH_ERROR_BACKOFF_MS 500U
/** @brief 写一个 64 KiB 约消耗的空闲块数（19 个逻辑组加文件系统元数据），只用于估算还要写几次。 */
#define FOREGROUND_FLASH_BLOCKS_PER_WRITE 21U
/**
 * @brief FTL 回收水位占预留块的百分比。
 * @note  复制自 Components/flash_ftl 的 FLASH_FTL_GC_START_RESERVE_PERCENT、STOP（该头是私有的，不能包含），
 *        公式 (预留块 × 百分比 + 99) / 100 同样取自 flash_ftl.c；FTL 改了这些值这里要同步，日志里会同时给出算出的水位。
 */
#define FOREGROUND_FLASH_GC_START_PERCENT 75U
#define FOREGROUND_FLASH_GC_STOP_PERCENT 90U

/** @brief #199：Flash 循环的目标字节数，以及 16 MiB 文件的写入时限（超过就按已写的字节数收尾，日志里标 partial）。 */
#define FOREGROUND_CYCLE_1M_BYTES (1024U * 1024U)
#define FOREGROUND_CYCLE_16M_BYTES (16U * 1024U * 1024U)
#define FOREGROUND_CYCLE_16M_WRITE_CAP_MS 600000U

/** @brief ui_cell 合成场景：6 KiB，每 16 ms 写一次。 */
#define FOREGROUND_CELL_BYTES 6144U
#define FOREGROUND_CELL_PERIOD_MS 16U
/** @brief 合成数据源在壁纸资源区内的起点偏移（壁纸共 0x38400 B）。 */
#define FOREGROUND_CELL_SOURCE_OFFSET 0x30000U

/* 壁纸资源由 Resource Service 在任务启动前放进 SDRAM，运行期只读；这里只把它当作 SDRAM 里的合成数据源。 */
extern const uint8_t __external_resource_wallpaper_start__[];

/* 32 KiB 读缓冲和 ui_cell 的格子与 GUI 副本放 AXI SRAM（用现有的 .lvgl_large_ram_array 段，不改链接脚本）。 */
static uint8_t foreground_txt_buffer[FOREGROUND_TXT_CHUNK_BYTES]
    __attribute__((section(".lvgl_large_ram_array"), aligned(32)));
static uint8_t foreground_cell_storage[FOREGROUND_CELL_BYTES]
    __attribute__((section(".lvgl_large_ram_array"), aligned(32)));
static uint8_t foreground_cell_gui_copy[FOREGROUND_CELL_BYTES]
    __attribute__((section(".lvgl_large_ram_array"), aligned(32)));

static ForegroundLoadStatsTypeDef foreground_load_stats_data;

static Service_Filesystem_FileHandleTypeDef foreground_txt_file;
static bool foreground_txt_open;
static uint64_t foreground_txt_size;
static uint64_t foreground_txt_offset;
static uint32_t foreground_txt_iteration;
static uint32_t foreground_txt_rng = 0x2545F491U;
static TickType_t foreground_txt_retry_tick;

typedef enum
{
    FOREGROUND_FLASH_PREPARE = 0U,
    FOREGROUND_FLASH_WRITE,
    FOREGROUND_FLASH_RECLAIM
} ForegroundFlashStateTypeDef;

static ForegroundFlashStateTypeDef foreground_flash_state;
static Service_Filesystem_FileHandleTypeDef foreground_flash_file;
static bool foreground_flash_open;
static uint32_t foreground_flash_written;
static uint32_t foreground_flash_chunk_index;
static uint32_t foreground_flash_idle_in_row;
static uint32_t foreground_flash_stage_calls;

static UiCell foreground_cell;
static uint32_t foreground_cell_gui_seen;
static volatile bool foreground_cell_active;
static TickType_t foreground_cell_next_tick;
static uint32_t foreground_cell_counter;

/**
 * @brief Service 状态值的短名，写进日志。
 * @param[in] status Service_StatusTypeDef 的数值。
 * @return 静态字符串。
 */
const char *foreground_status_name(uint32_t status)
{
    switch ((Service_StatusTypeDef)status)
    {
        case SERVICE_OK: return "OK";
        case SERVICE_ERROR: return "ERROR";
        case SERVICE_INVALID_PARAM: return "INVALID_PARAM";
        case SERVICE_NOT_READY: return "NOT_READY";
        case SERVICE_BUSY: return "BUSY";
        case SERVICE_TIMEOUT: return "TIMEOUT";
        case SERVICE_NO_FILESYSTEM: return "NO_FILESYSTEM";
        case SERVICE_INVALID_HANDLE: return "INVALID_HANDLE";
        case SERVICE_NO_MEDIA: return "NO_MEDIA";
        case SERVICE_NO_SPACE: return "NO_SPACE";
        default: return "?";
    }
}

/**
 * @brief xorshift32 伪随机数，用来挑随机页。
 * @return 下一个随机数。
 */
static uint32_t foreground_load_random(void)
{
    foreground_txt_rng ^= foreground_txt_rng << 13U;
    foreground_txt_rng ^= foreground_txt_rng >> 17U;
    foreground_txt_rng ^= foreground_txt_rng << 5U;
    return foreground_txt_rng;
}

/**
 * @brief 初始化负载模块：建 ui_cell 的格子。
 * @note  前台任务启动时调用一次，早于 GUI 读格子（GUI 只在 P4 打开读取开关之后才读）。
 */
void foreground_load_init(void)
{
    ui_cell_init(&foreground_cell, foreground_cell_storage, sizeof(foreground_cell_storage), NULL, 0U);
    memset(foreground_cell_storage, 0, sizeof(foreground_cell_storage));
    memset(foreground_cell_gui_copy, 0, sizeof(foreground_cell_gui_copy));
}

/**
 * @brief 清零负载统计（阶段开始时调用）。
 */
void foreground_load_stats_reset(void)
{
    memset(&foreground_load_stats_data, 0, sizeof(foreground_load_stats_data));
    ui_cell_stats_reset(&foreground_cell);
    spike_audio_read_reset();
    foreground_cycle_reset();
    foreground_openlast_reset();
}

/**
 * @brief 读负载统计。
 * @return 统计指针；只有前台任务读写。
 */
const ForegroundLoadStatsTypeDef *foreground_load_stats(void)
{
    return &foreground_load_stats_data;
}

/**
 * @brief 读 ui_cell 格子，用来取它的统计。
 * @return 格子指针。
 */
const UiCell *foreground_load_cell(void)
{
    return &foreground_cell;
}

/**
 * @brief 打开 SD 上的大 TXT 并记下文件大小。
 * @return true 已打开；false 失败，状态记入统计。
 */
static bool foreground_txt_open_file(void)
{
    Service_StatusTypeDef status = Service_Filesystem_OpenFile(
        SERVICE_FILESYSTEM_VOLUME_SD, FOREGROUND_TXT_PATH, SERVICE_FILESYSTEM_FILE_MODE_READ, &foreground_txt_file);

    if (status == SERVICE_OK)
    {
        status = Service_Filesystem_GetFileSize(foreground_txt_file, &foreground_txt_size);
        if (status != SERVICE_OK)
        {
            (void)Service_Filesystem_CloseFile(foreground_txt_file);
        }
    }
    if (status != SERVICE_OK)
    {
        foreground_load_stats_data.TxtLastStatus = (uint32_t)status;
        return false;
    }

    foreground_txt_open = true;
    foreground_txt_offset = 0U;
    return true;
}

/**
 * @brief 记下 P3 开始时的块计数，按 FTL 公式算出回收水位，并估算要写多少才能到水位以下。
 * @param[out] detail 写入 PHASE start 行的补充信息。
 * @param[in] detail_size detail 的字节数。
 * @note  块计数读取失败时只给出提示，P3 仍照常写入和回收，只是没有水位信息。
 */
static void foreground_flash_begin_plan(char *detail, size_t detail_size)
{
    Service_Filesystem_SpikeFlashBlocksTypeDef blocks;
    ForegroundLoadStatsTypeDef *stats = &foreground_load_stats_data;
    uint32_t need;

    if (!Service_Filesystem_SpikeFlashBlocks(&blocks))
    {
        (void)snprintf(detail, detail_size, "blocks=unavailable");
        return;
    }

    stats->FlashBlocksValid = true;
    stats->FlashBlocksStart = blocks;
    stats->FlashBlocksNow = blocks;
    stats->FlashGcLow = (blocks.ReserveBlocks * FOREGROUND_FLASH_GC_START_PERCENT + 99U) / 100U;
    stats->FlashGcHigh = (blocks.ReserveBlocks * FOREGROUND_FLASH_GC_STOP_PERCENT + 99U) / 100U;
    need = (blocks.FreeBlocks > stats->FlashGcLow) ? (blocks.FreeBlocks - stats->FlashGcLow) : 0U;
    (void)snprintf(detail,
                   detail_size,
                   "free=%lu stale=%lu low=%lu high=%lu need=%lu",
                   (unsigned long)blocks.FreeBlocks,
                   (unsigned long)blocks.StaleBlocks,
                   (unsigned long)stats->FlashGcLow,
                   (unsigned long)stats->FlashGcHigh,
                   (unsigned long)need);
}

/**
 * @brief 判断一个阶段的负载能否开始，并做开始前的准备。
 * @param[in] kind 负载种类。
 * @param[out] detail 能开始时写入补充信息（如文件大小），不能开始时写入原因。
 * @param[in] detail_size detail 的字节数。
 * @return true 可以开始；false 本阶段“未测”，调用者应跳过。
 */
bool foreground_load_begin(ForegroundLoadKindTypeDef kind, char *detail, size_t detail_size)
{
    Service_Filesystem_FileInfoTypeDef info;
    Service_StatusTypeDef status;

    detail[0] = '\0';
    switch (kind)
    {
        case FOREGROUND_LOAD_TXT:
            status = Service_Filesystem_GetFileInfo(SERVICE_FILESYSTEM_VOLUME_SD, FOREGROUND_TXT_PATH, &info);
            if (status != SERVICE_OK)
            {
                (void)snprintf(detail, detail_size, "%s status=%s", FOREGROUND_TXT_PATH,
                               foreground_status_name((uint32_t)status));
                return false;
            }
            if (!foreground_txt_open_file())
            {
                (void)snprintf(detail, detail_size, "%s open status=%s", FOREGROUND_TXT_PATH,
                               foreground_status_name(foreground_load_stats_data.TxtLastStatus));
                return false;
            }
            foreground_txt_iteration = 0U;
            (void)snprintf(detail, detail_size, "size=%lu%s", (unsigned long)info.Size,
                           (info.Size < FOREGROUND_TXT_MIN_BYTES) ? " (<5MiB)" : "");
            return true;

        case FOREGROUND_LOAD_FLASH:
            status = Service_Filesystem_CreateDirectory(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_FLASH_DIR);
            if ((status != SERVICE_OK) && (status != SERVICE_BUSY))
            {
                (void)snprintf(detail, detail_size, "flash volume status=%s",
                               foreground_status_name((uint32_t)status));
                return false;
            }
            foreground_flash_state = FOREGROUND_FLASH_PREPARE;
            foreground_flash_open = false;
            foreground_flash_chunk_index = 0U;
            foreground_flash_begin_plan(detail, detail_size);
            return true;

        case FOREGROUND_LOAD_CELL:
            foreground_cell_gui_seen = foreground_cell.Sequence;
            foreground_cell_next_tick = xTaskGetTickCount();
            foreground_cell_counter = 0U;
            foreground_cell_active = true;
            return true;

        case FOREGROUND_LOAD_FLASH_CYCLE_1M:
            return foreground_cycle_begin(FOREGROUND_CYCLE_1M_BYTES, 0U, detail, detail_size);

        case FOREGROUND_LOAD_FLASH_CYCLE_16M:
            return foreground_cycle_begin(FOREGROUND_CYCLE_16M_BYTES, FOREGROUND_CYCLE_16M_WRITE_CAP_MS, detail,
                                          detail_size);

        case FOREGROUND_LOAD_SD_OPEN_LAST:
            return foreground_openlast_begin(detail, detail_size);

        default:
            return true;
    }
}

/**
 * @brief 结束一个阶段的负载：关文件、删测试文件、关 GUI 读格子的开关。
 * @param[in] kind 负载种类。
 */
void foreground_load_end(ForegroundLoadKindTypeDef kind)
{
    switch (kind)
    {
        case FOREGROUND_LOAD_TXT:
            if (foreground_txt_open)
            {
                (void)Service_Filesystem_CloseFile(foreground_txt_file);
                foreground_txt_open = false;
            }
            break;

        case FOREGROUND_LOAD_FLASH:
            if (foreground_flash_open)
            {
                (void)Service_Filesystem_CloseFile(foreground_flash_file);
                foreground_flash_open = false;
            }
            (void)Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_FLASH_PATH);
            break;

        case FOREGROUND_LOAD_CELL:
            foreground_cell_active = false;
            break;

        case FOREGROUND_LOAD_FLASH_CYCLE_1M:
        case FOREGROUND_LOAD_FLASH_CYCLE_16M:
            foreground_cycle_end();
            break;

        case FOREGROUND_LOAD_SD_OPEN_LAST:
            foreground_openlast_end();
            break;

        default:
            break;
    }
}

/**
 * @brief 统计 buffer 里的换行数。
 * @param[in] data 数据。
 * @param[in] length 字节数。
 * @return 换行（0x0A）个数。
 */
static uint32_t foreground_txt_count_newlines(const uint8_t *data, uint32_t length)
{
    uint32_t count = 0U;
    uint32_t index;

    for (index = 0U; index < length; index++)
    {
        if (data[index] == (uint8_t)'\n')
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief 记下一次失败的 TXT 读取，并通知拔卡时间线；之后关掉文件等待重开。
 * @param[in] status 失败的 Service 状态。
 * @param[in] hold_us 该次调用持锁多久。
 */
static void foreground_txt_fail(Service_StatusTypeDef status, uint32_t hold_us)
{
    foreground_load_stats_data.TxtErrors++;
    foreground_load_stats_data.TxtLastStatus = (uint32_t)status;
    spike_unplug_note_foreground_read((uint32_t)status, hold_us);
    (void)Service_Filesystem_CloseFile(foreground_txt_file);
    foreground_txt_open = false;
    foreground_txt_retry_tick = xTaskGetTickCount();
}

/**
 * @brief 在随机页起点读 4 KiB 模拟翻页，然后把顺序读指针放回去。
 */
static void foreground_txt_page_turn(void)
{
    uint32_t pages = (uint32_t)(foreground_txt_size / FOREGROUND_TXT_PAGE_BYTES);
    uint32_t transferred = 0U;
    uint64_t target;
    Service_StatusTypeDef status;

    if (pages == 0U)
    {
        pages = 1U;
    }
    target = (uint64_t)(foreground_load_random() % pages) * FOREGROUND_TXT_PAGE_BYTES;

    status = Service_Filesystem_SeekFile(foreground_txt_file, target);
    if (status == SERVICE_OK)
    {
        status = Service_Filesystem_ReadFile(foreground_txt_file, foreground_txt_buffer,
                                             FOREGROUND_TXT_PAGE_READ_BYTES, &transferred);
        if (status == SERVICE_OK)
        {
            foreground_stat_add(&foreground_load_stats_data.Txt4k, Service_Filesystem_SpikeLastHoldUs());
            status = Service_Filesystem_SeekFile(foreground_txt_file, foreground_txt_offset);
        }
    }
    if (status != SERVICE_OK)
    {
        foreground_txt_fail(status, Service_Filesystem_SpikeLastHoldUs());
    }
}

/**
 * @brief TXT 负载的一步：顺序读一个 32 KiB 块并统计换行，每隔几块插一次随机翻页。
 */
static void foreground_txt_step(void)
{
    uint32_t transferred = 0U;
    Service_StatusTypeDef status;
    uint32_t hold_us;

    if (!foreground_txt_open)
    {
        if ((TickType_t)(xTaskGetTickCount() - foreground_txt_retry_tick) < pdMS_TO_TICKS(FOREGROUND_TXT_RETRY_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(50U));
            return;
        }
        foreground_txt_retry_tick = xTaskGetTickCount();
        if (foreground_txt_open_file())
        {
            foreground_logf("TXT reopened size=%lu", (unsigned long)foreground_txt_size);
        }
        return;
    }

    status = Service_Filesystem_ReadFile(foreground_txt_file, foreground_txt_buffer,
                                         FOREGROUND_TXT_CHUNK_BYTES, &transferred);
    hold_us = Service_Filesystem_SpikeLastHoldUs();
    if (status != SERVICE_OK)
    {
        foreground_txt_fail(status, hold_us);
        return;
    }

    if (transferred == FOREGROUND_TXT_CHUNK_BYTES)
    {
        foreground_stat_add(&foreground_load_stats_data.Txt32k, hold_us);
    }
    foreground_load_stats_data.TxtNewlines += foreground_txt_count_newlines(foreground_txt_buffer, transferred);
    foreground_txt_offset += transferred;

    if (transferred < FOREGROUND_TXT_CHUNK_BYTES)
    {
        foreground_load_stats_data.TxtPasses++;
        foreground_txt_offset = 0U;
        status = Service_Filesystem_SeekFile(foreground_txt_file, 0U);
        if (status != SERVICE_OK)
        {
            foreground_txt_fail(status, Service_Filesystem_SpikeLastHoldUs());
            return;
        }
    }

    foreground_txt_iteration++;
    if ((foreground_txt_iteration % FOREGROUND_TXT_PAGE_EVERY) == 0U)
    {
        foreground_txt_page_turn();
    }

    vTaskDelay(pdMS_TO_TICKS(FOREGROUND_TXT_PACE_MS));
}

/**
 * @brief Flash 负载出错后的善后：关文件、删测试文件、回到准备态并退避。
 * @param[in] status 出错的 Service 状态。
 */
static void foreground_flash_fail(Service_StatusTypeDef status)
{
    foreground_load_stats_data.FlashErrors++;
    if (foreground_flash_open)
    {
        (void)Service_Filesystem_CloseFile(foreground_flash_file);
        foreground_flash_open = false;
    }
    (void)Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_FLASH_PATH);
    foreground_flash_state = FOREGROUND_FLASH_PREPARE;
    foreground_logf("FLASHLOAD error status=%s", foreground_status_name((uint32_t)status));
    vTaskDelay(pdMS_TO_TICKS(FOREGROUND_FLASH_ERROR_BACKOFF_MS));
}

/**
 * @brief Flash 负载的准备态：删旧文件，新建 load.bin。
 */
static void foreground_flash_prepare(void)
{
    Service_StatusTypeDef status = Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                                                FOREGROUND_FLASH_PATH);

    if (status == SERVICE_OK)
    {
        status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_FLASH_PATH,
                                             SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW, &foreground_flash_file);
    }
    if (status != SERVICE_OK)
    {
        foreground_flash_fail(status);
        return;
    }

    foreground_flash_open = true;
    foreground_flash_written = 0U;
    foreground_flash_state = FOREGROUND_FLASH_WRITE;
}

/**
 * @brief Flash 负载的写态：写 64 KiB 并同步；写到约 1 MiB 后关闭并删除，转入回收。
 */
static void foreground_flash_write(void)
{
    const uint8_t *source = &__external_resource_wallpaper_start__[(foreground_flash_chunk_index % 3U) *
                                                                   FOREGROUND_FLASH_CHUNK_BYTES];
    uint32_t transferred = 0U;
    Service_StatusTypeDef status = Service_Filesystem_WriteFile(foreground_flash_file, source,
                                                                FOREGROUND_FLASH_CHUNK_BYTES, &transferred);

    if ((status == SERVICE_OK) && (transferred != FOREGROUND_FLASH_CHUNK_BYTES))
    {
        status = SERVICE_NO_SPACE;
    }
    if (status != SERVICE_OK)
    {
        foreground_flash_fail(status);
        return;
    }
    foreground_stat_add(&foreground_load_stats_data.FlashWrite, Service_Filesystem_SpikeLastHoldUs());

    status = Service_Filesystem_SyncFile(foreground_flash_file);
    if (status != SERVICE_OK)
    {
        foreground_flash_fail(status);
        return;
    }
    foreground_stat_add(&foreground_load_stats_data.FlashSync, Service_Filesystem_SpikeLastHoldUs());

    foreground_flash_written += transferred;
    foreground_flash_chunk_index++;
    if (foreground_flash_written >= FOREGROUND_FLASH_TARGET_BYTES)
    {
        status = Service_Filesystem_CloseFile(foreground_flash_file);
        foreground_flash_open = (status != SERVICE_OK);
        if (status == SERVICE_OK)
        {
            status = Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FOREGROUND_FLASH_PATH);
        }
        if (status != SERVICE_OK)
        {
            foreground_flash_fail(status);
            return;
        }
        foreground_load_stats_data.FlashFiles++;
        foreground_flash_idle_in_row = 0U;
        foreground_flash_stage_calls = 0U;
        foreground_flash_state = FOREGROUND_FLASH_RECLAIM;
    }

    vTaskDelay(pdMS_TO_TICKS(FOREGROUND_FLASH_PACE_MS));
}

/**
 * @brief Flash 负载的回收态：删除之后反复调用回收，并把真擦除和空转分开统计。
 * @note  Storage 主循环在音频流打开时不回收，所以由前台任务调用。口径：同一次 Flash 卷锁内读回收前后的
 *        FTL `SessionErases`，差值不为零就是真擦除（FTL 在擦除并校验完一块后才加一），否则是空转。
 *        真擦除继续回收，连续空转几次或到阶段调用上限就回到写入。
 */
static void foreground_flash_reclaim(void)
{
    ForegroundLoadStatsTypeDef *stats = &foreground_load_stats_data;
    Service_Filesystem_SpikeFlashBlocksTypeDef before;
    Service_Filesystem_SpikeFlashBlocksTypeDef after;
    Service_StatusTypeDef status = Service_Filesystem_SpikeReclaimFlashMeasured(&before, &after);
    const uint32_t hold_us = Service_Filesystem_SpikeLastHoldUs();
    uint32_t erased;

    if (status != SERVICE_OK)
    {
        foreground_flash_fail(status);
        return;
    }

    erased = after.SessionErases - before.SessionErases;
    if (stats->FlashBlocksValid)
    {
        stats->FlashBlocksNow = after;
    }
    if (erased != 0U)
    {
        foreground_stat_add(&stats->FlashReclaimErase, hold_us);
        stats->FlashErasesInReclaim += erased;
        foreground_flash_idle_in_row = 0U;
    }
    else
    {
        foreground_stat_add(&stats->FlashReclaimIdle, hold_us);
        foreground_flash_idle_in_row++;
    }

    foreground_flash_stage_calls++;
    if ((foreground_flash_idle_in_row >= FOREGROUND_FLASH_IDLE_RECLAIMS) ||
        (foreground_flash_stage_calls >= FOREGROUND_FLASH_RECLAIM_STAGE_CAP))
    {
        foreground_flash_state = FOREGROUND_FLASH_PREPARE;
    }
    vTaskDelay(pdMS_TO_TICKS(FOREGROUND_FLASH_RECLAIM_PACE_MS));
}

/**
 * @brief 汇总前刷新负载的“当前值”：Flash 负载重新读一次块计数。
 * @param[in] kind 当前阶段的负载种类。
 * @note  回收之外的时间（正在写文件、时间到）块计数已经变了，所以输出前读一次，别用最近一次回收之后的旧值。
 */
void foreground_load_refresh(ForegroundLoadKindTypeDef kind)
{
    Service_Filesystem_SpikeFlashBlocksTypeDef blocks;

    if ((kind == FOREGROUND_LOAD_FLASH) && foreground_load_stats_data.FlashBlocksValid &&
        Service_Filesystem_SpikeFlashBlocks(&blocks))
    {
        foreground_load_stats_data.FlashBlocksNow = blocks;
    }
    if ((kind == FOREGROUND_LOAD_FLASH_CYCLE_1M) || (kind == FOREGROUND_LOAD_FLASH_CYCLE_16M))
    {
        foreground_cycle_refresh();
    }
}

/**
 * @brief 一个阶段的负载是否已经达到目标，可以提前结束。
 * @param[in] kind 当前阶段的负载种类。
 * @return P3 恒为 false。第二遍板测发现：擦够次数就退出时 P3 只跑了 9 s，音频读取样本为 0、手动切歌也赶不上，
 *         所以 P3 改为固定时长跑满，真擦除次数只统计。#199 的 16 MiB 循环删除完成、按路径打开重复够次数时为 true，
 *         1 MiB 循环按阶段时限结束。
 */
bool foreground_load_finished(ForegroundLoadKindTypeDef kind)
{
    switch (kind)
    {
        case FOREGROUND_LOAD_FLASH_CYCLE_16M:
            return foreground_cycle_finished();
        case FOREGROUND_LOAD_SD_OPEN_LAST:
            return foreground_openlast_finished();
        default:
            return false;
    }
}

/**
 * @brief Flash 负载的一步。
 */
static void foreground_flash_step(void)
{
    switch (foreground_flash_state)
    {
        case FOREGROUND_FLASH_PREPARE:
            foreground_flash_prepare();
            break;
        case FOREGROUND_FLASH_WRITE:
            foreground_flash_write();
            break;
        default:
            foreground_flash_reclaim();
            break;
    }
}

/**
 * @brief ui_cell 合成负载的一步：到点就把 SDRAM 里的 6 KiB 数据写进格子。
 * @note  每 16 ms 一次，按截止时刻推进，落后太多时重新对齐。
 */
static void foreground_cell_step(void)
{
    const TickType_t now = xTaskGetTickCount();
    const uint8_t *source;

    if ((int32_t)(now - foreground_cell_next_tick) < 0)
    {
        vTaskDelay(1U);
        return;
    }

    source = &__external_resource_wallpaper_start__[FOREGROUND_CELL_SOURCE_OFFSET +
                                                    ((foreground_cell_counter % 8U) * 512U)];
    ui_cell_write(&foreground_cell, source);
    foreground_cell_counter++;

    foreground_cell_next_tick += pdMS_TO_TICKS(FOREGROUND_CELL_PERIOD_MS);
    if ((int32_t)(now - foreground_cell_next_tick) > (int32_t)pdMS_TO_TICKS(5U * FOREGROUND_CELL_PERIOD_MS))
    {
        foreground_cell_next_tick = now + pdMS_TO_TICKS(FOREGROUND_CELL_PERIOD_MS);
    }
}

/**
 * @brief 执行一个负载的一小步（至多一次 32 KiB 读、一次 64 KiB 写或一次合成拷贝）。
 * @param[in] kind 当前阶段的负载种类。
 */
void foreground_load_step(ForegroundLoadKindTypeDef kind)
{
    switch (kind)
    {
        case FOREGROUND_LOAD_TXT:
            foreground_txt_step();
            break;
        case FOREGROUND_LOAD_FLASH:
            foreground_flash_step();
            break;
        case FOREGROUND_LOAD_CELL:
            foreground_cell_step();
            break;
        case FOREGROUND_LOAD_FLASH_CYCLE_1M:
        case FOREGROUND_LOAD_FLASH_CYCLE_16M:
            foreground_cycle_step();
            break;
        case FOREGROUND_LOAD_SD_OPEN_LAST:
            foreground_openlast_step();
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(50U));
            break;
    }
}

/**
 * @brief GUI 每圈调用：P4 打开开关后，从格子读最新数据到 GUI 副本，永不等待。
 * @note  GUI Task 调用；序号没变不碰锁，拿不到锁就保持原样、下一圈再试，失败次数记在格子的统计里。
 */
void foreground_gui_step(void)
{
    if (foreground_cell_active)
    {
        (void)ui_cell_read_if_changed(&foreground_cell, foreground_cell_gui_copy, &foreground_cell_gui_seen);
    }
}
