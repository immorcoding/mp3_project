/**
 ******************************************************************************
 * @file    background_flash_pressure.c
 * @brief   Flash 压力测试钩子作业：写入、回收、删除，日志带空闲块数和真擦除次数（见 background_flash_pressure.h）。
 *
 * @details
 *          状态机（每步最多一次文件调用，返回多少毫秒后再调）：
 *          WAIT_VOLUME（Flash 卷没挂载，轮询卷序号，不碰 Flash 卷）→ CLEANUP（删上次遗留）→ OPEN（新建，CREATE_NEW）
 *          → WRITE（一块 32 KiB）→ RECLAIM（可选，一次回收）→ 回到 WRITE，直到写满 4 MiB 或卷满 → CLOSE → REMOVE → OPEN。
 *          任何一次文件调用返回"卷不可用"（NO_MEDIA、NOT_READY、INVALID_HANDLE）都不是故障：回到 WAIT_VOLUME，
 *          卷再就绪后从 CLEANUP 重新开始，不对旧句柄调关闭。别的失败一次就停：记一条 ERROR，关掉文件，
 *          之后只轮询卷序号，序号变了才重新开始（与 #202 F 节"回收失败一次就停"同一规则）。
 *
 *          日志（标签 `FLASHHOOK`，串口里标签后面有冒号；正文）：
 *          - 开始与每 `BACKGROUND_HOOK_FLASH_PRESSURE_LOG_PERIOD_MS` 一行：
 *            `<start|run> free=<空闲块> erases=<真擦除次数，钩子开始之后的增量> wr_kib=<已写 KiB> files=<已删文件数>
 *            wmax=<最长一次写 ms> rmax=<最长一次回收 ms> dmax=<最长一次删除 ms>`，最大值是自上一行起的（含等锁时间）。
 *          - 卷满：WARN `flash full after <字节> B, closing file`，一次会话只记一次。
 *          - 停下：ERROR `stopped at=<步骤名> status=<Service 状态数值> <FTL 失败诊断>[ pre=<空闲>/<有效>/<失效> req=<字节> file=<字节>]`，
 *            一行不超过 127 字符（诊断片段见 `background_flash_diag.h`；`pre`、`req`、`file` 只在写步失败时有）；之后到 Flash 卷序号变化为止不再动。
 *          启动那一行 `HOOK: flash_pressure ON …` 由 `background_flash_pressure_announce()` 投（WARN）。
 *
 *          空闲块数和擦除次数来自 `Platform_Flash_GetVolumeDiagnostics()`，在 Flash 卷锁外读，只给人看：
 *          别的任务正在操作 FTL 时读到的值可能差一两个，不用来做判断（只有 `FREE_FLOOR` 一处用它决定是否暂停写入，
 *          差一两个无妨）。
 ******************************************************************************
 */

#include "APP/tasks/background/background_flash_diag.h"
#include "APP/tasks/background/background_flash_pressure.h"
#include "APP/tasks/background/background_task_config.h"
#include "APP/tasks/volume/volume_event.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Platform/flash/platform_flash.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/filesystem_types.h"
#include "Service/log/log_service.h"

_Static_assert(BACKGROUND_HOOK_FLASH_PRESSURE_CHUNK_BYTES <= SERVICE_FILESYSTEM_IO_MAX_BYTES,
               "Flash pressure chunk exceeds the single-call I/O limit of the file interface");
_Static_assert(BACKGROUND_HOOK_FLASH_PRESSURE_CHUNK_BYTES != 0U, "Flash pressure chunk must not be empty");
_Static_assert(BACKGROUND_HOOK_FLASH_PRESSURE_FILE_BYTES >= BACKGROUND_HOOK_FLASH_PRESSURE_CHUNK_BYTES,
               "Flash pressure file must hold at least one chunk");

/** @brief 压力文件在 Flash 卷根目录里的路径（8.3 名字，开机后第一次挂载先删上次遗留）。 */
#define FLASH_PRESSURE_PATH "FLASHP.TMP"

/** @brief 数据行与停下行的日志标签。 */
static const char flash_pressure_tag[] = "FLASHHOOK";

/** @brief 启动日志标签（与迟到 DMA 钩子的 `HOOK: late_dma ON` 同一约定）。 */
static const char flash_pressure_hook_tag[] = "HOOK";

/** @brief 状态机的步骤。 */
typedef enum
{
    FLASH_PRESSURE_WAIT_VOLUME = 0, /**< Flash 卷没挂载：轮询卷序号。 */
    FLASH_PRESSURE_CLEANUP,         /**< 删上次遗留的压力文件。 */
    FLASH_PRESSURE_OPEN,            /**< 新建压力文件。 */
    FLASH_PRESSURE_WRITE,           /**< 写一块。 */
    FLASH_PRESSURE_RECLAIM,         /**< 一次回收。 */
    FLASH_PRESSURE_CLOSE,           /**< 关闭文件。 */
    FLASH_PRESSURE_REMOVE,          /**< 删除文件。 */
    FLASH_PRESSURE_STOPPED          /**< 失败过一次，等卷序号变化。 */
} FlashPressure_PhaseTypeDef;

/** @brief 日志里的步骤名，下标与 `FlashPressure_PhaseTypeDef` 一致。 */
static const char *const flash_pressure_phase_names[] = {
    "wait", "cleanup", "open", "write", "reclaim", "close", "remove", "stopped",
};

/** @brief 作业状态；只有后台任务访问。零初始化即"等卷"。 */
typedef struct
{
    FlashPressure_PhaseTypeDef phase;          /**< 下一步做什么。 */
    uint32_t volume_sequence;                  /**< 本次会话开始时的 Flash 卷序号。 */
    Service_Filesystem_FileHandleTypeDef file; /**< 压力文件的句柄。 */
    bool file_open;                            /**< 句柄有效（打开了还没关）。 */
    bool file_done;                            /**< 当前文件写满或卷满，该关闭了。 */
    bool stop_after_close;                     /**< 关闭之后不删除，直接停下（写入或回收失败的善后）。 */
    bool full_reported;                        /**< 本次会话已记过卷满。 */
    bool baseline_valid;                       /**< 擦除基数已取。 */
    uint32_t file_bytes;                       /**< 当前文件已写字节。 */
    uint32_t busy_retries;                     /**< 新建遇到 SERVICE_BUSY 已重试几次。 */
    uint64_t total_bytes;                      /**< 钩子开始以来写入的字节。 */
    uint32_t files_done;                       /**< 钩子开始以来写满并删除的文件数。 */
    uint32_t erase_baseline;                   /**< 钩子开始时 FTL 的擦除累计。 */
    TickType_t last_log;                       /**< 上一行统计的 tick。 */
    uint32_t write_max_ms;                     /**< 自上一行起最长一次写。 */
    uint32_t reclaim_max_ms;                   /**< 自上一行起最长一次回收。 */
    uint32_t remove_max_ms;                    /**< 自上一行起最长一次删除。 */
    Platform_Flash_VolumeDiagnosticsTypeDef pre_write; /**< 最近一次写之前的 FTL 诊断快照，停下行用（失败后 FTL 不就绪，有效/失效块数读作 0）。 */
    bool pre_write_valid;                      /**< `pre_write` 读到了。 */
    uint32_t write_request;                    /**< 最近一次写请求的字节数。 */
} FlashPressure_StateTypeDef;

static FlashPressure_StateTypeDef flash_pressure;

/**
 * @brief  从 start 到现在经过的毫秒数。
 * @param[in] start 起点 tick。
 * @return 毫秒数（无符号差值，tick 回绕也对）。
 */
static uint32_t flash_pressure_elapsed_ms(TickType_t start)
{
    return (uint32_t)(((uint64_t)(TickType_t)(xTaskGetTickCount() - start) * 1000ULL) / (uint64_t)configTICK_RATE_HZ);
}

/**
 * @brief  读 FTL 的空闲块数和擦除累计。
 * @param[out] free_blocks 已确认擦净的空闲块数。
 * @param[out] erases FTL 本次 Init 以来的数据块擦除累计。
 * @retval true 读到。
 * @retval false FTL 还没绑定。
 */
static bool flash_pressure_read_stats(uint32_t *free_blocks, uint32_t *erases)
{
    Platform_Flash_VolumeDiagnosticsTypeDef diagnostics;

    if (Platform_Flash_GetVolumeDiagnostics(&diagnostics) != PLATFORM_OK)
    {
        return false;
    }

    *free_blocks = diagnostics.FreeBlocks;
    *erases = diagnostics.SessionErases;
    return true;
}

/**
 * @brief  投一行统计并清零三个最大值。
 * @param[in] kind 行首的词：`start` 或 `run`。
 * @param[in] now 当前 tick，记为上一行时间。
 * @note   读不到 FTL 诊断就不投（卷已挂载时不会发生）；擦除次数是钩子开始之后的增量，
 *         FTL 的累计里含格式化等钩子之前的擦除。
 */
static void flash_pressure_post_stats(const char *kind, TickType_t now)
{
    char text[128];
    uint32_t free_blocks;
    uint32_t erases;

    if (!flash_pressure_read_stats(&free_blocks, &erases))
    {
        return;
    }

    if (!flash_pressure.baseline_valid)
    {
        flash_pressure.erase_baseline = erases;
        flash_pressure.baseline_valid = true;
    }
    if (erases < flash_pressure.erase_baseline)
    {
        flash_pressure.erase_baseline = 0U; /* FTL 重新 Init 过，累计从 0 起。 */
    }

    (void)snprintf(text,
                   sizeof(text),
                   "%s free=%lu erases=%lu wr_kib=%lu files=%lu wmax=%lums rmax=%lums dmax=%lums",
                   kind,
                   (unsigned long)free_blocks,
                   (unsigned long)(erases - flash_pressure.erase_baseline),
                   (unsigned long)(flash_pressure.total_bytes >> 10),
                   (unsigned long)flash_pressure.files_done,
                   (unsigned long)flash_pressure.write_max_ms,
                   (unsigned long)flash_pressure.reclaim_max_ms,
                   (unsigned long)flash_pressure.remove_max_ms);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, flash_pressure_tag, text);

    flash_pressure.write_max_ms = 0U;
    flash_pressure.reclaim_max_ms = 0U;
    flash_pressure.remove_max_ms = 0U;
    flash_pressure.last_log = now;
}

/**
 * @brief  回到"等卷"：句柄作废（卸载使它失效，不去关）。
 */
static void flash_pressure_enter_wait(void)
{
    flash_pressure.phase = FLASH_PRESSURE_WAIT_VOLUME;
    flash_pressure.file_open = false;
    flash_pressure.file.Token = 0U;
}

/**
 * @brief  Flash 卷刚就绪（或卷序号变了）：从头开始一次会话，投 `start` 行。
 * @param[in] sequence 此刻的 Flash 卷序号。
 * @param[in] now 当前 tick。
 */
static void flash_pressure_start_session(uint32_t sequence, TickType_t now)
{
    flash_pressure.volume_sequence = sequence;
    flash_pressure.file_open = false;
    flash_pressure.file.Token = 0U;
    flash_pressure.file_done = false;
    flash_pressure.stop_after_close = false;
    flash_pressure.full_reported = false;
    flash_pressure.file_bytes = 0U;
    flash_pressure.busy_retries = 0U;
    flash_pressure.phase = FLASH_PRESSURE_CLEANUP;
    flash_pressure_post_stats("start", now);
    flash_pressure.last_log = now;
}

/**
 * @brief  失败一次就停：记一条 ERROR（带 FTL 失败诊断，一行不超过 127 字符）；文件还开着就先关闭，不删除。
 * @param[in] at 失败发生的步骤。
 * @param[in] status 文件接口返回的状态。
 * @return 这一步返回的等待毫秒数。
 * @note   行里写：停在哪一步、文件接口的状态数值（1 是 `SERVICE_ERROR`，FTL 的任何非 OK 结果都压成它）、`background_flash_diag.h` 的诊断片段
 *         （FTL 的具体结果、请求类别、失败次数、失败后的空闲块等）；写步另加失败那次写之前的快照 `pre=<空闲>/<有效>/<失效>`、
 *         那次写请求的字节数 `req=` 与当前文件已写字节 `file=`（`rej`、`hw` 在最后，放不下先截断它们）。停下是一次性的，仍是一行，
 *         消息池核算不变（只是这一行变长，没有新增行）。
 */
static uint32_t flash_pressure_stop(FlashPressure_PhaseTypeDef at, Service_StatusTypeDef status)
{
    char text[128];
    char extra[64];

    (void)snprintf(text,
                   sizeof(text),
                   "stopped at=%s status=%lu",
                   flash_pressure_phase_names[at],
                   (unsigned long)status);
    extra[0] = '\0';
    if ((at == FLASH_PRESSURE_WRITE) && flash_pressure.pre_write_valid)
    {
        (void)snprintf(extra,
                       sizeof(extra),
                       " pre=%lu/%lu/%lu req=%lu file=%lu",
                       (unsigned long)flash_pressure.pre_write.FreeBlocks,
                       (unsigned long)flash_pressure.pre_write.ValidGroups,
                       (unsigned long)flash_pressure.pre_write.StaleBlocks,
                       (unsigned long)flash_pressure.write_request,
                       (unsigned long)flash_pressure.file_bytes);
    }
    else if (at == FLASH_PRESSURE_WRITE)
    {
        (void)snprintf(extra,
                       sizeof(extra),
                       " req=%lu file=%lu",
                       (unsigned long)flash_pressure.write_request,
                       (unsigned long)flash_pressure.file_bytes);
    }
    background_flash_diag_append(text, sizeof(text), extra);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR, flash_pressure_tag, text);

    if (flash_pressure.file_open)
    {
        flash_pressure.stop_after_close = true;
        flash_pressure.phase = FLASH_PRESSURE_CLOSE;
        return 0U;
    }

    flash_pressure.phase = FLASH_PRESSURE_STOPPED;
    return BACKGROUND_HOOK_FLASH_PRESSURE_WAIT_VOLUME_MS;
}

/**
 * @brief  文件调用失败的统一处理：卷不可用回到等卷，别的失败停下。
 * @param[in] at 失败发生的步骤。
 * @param[in] status 文件接口返回的状态。
 * @return 这一步返回的等待毫秒数。
 */
static uint32_t flash_pressure_fail(FlashPressure_PhaseTypeDef at, Service_StatusTypeDef status)
{
    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        flash_pressure_enter_wait();
        return BACKGROUND_HOOK_FLASH_PRESSURE_WAIT_VOLUME_MS;
    }

    return flash_pressure_stop(at, status);
}

/**
 * @brief  删上次遗留的压力文件；文件不在也无所谓，只看卷是否可用。
 * @return 这一步返回的等待毫秒数：0 紧接着新建；卷不可用时是轮询间隔。
 */
static uint32_t flash_pressure_step_cleanup(void)
{
    const Service_StatusTypeDef status = Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FLASH_PRESSURE_PATH);

    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        return flash_pressure_fail(FLASH_PRESSURE_CLEANUP, status);
    }

    flash_pressure.phase = FLASH_PRESSURE_OPEN;
    return 0U;
}

/**
 * @brief  新建压力文件；BUSY（文件已存在或槽用尽）先删遗留再重试，有次数上限。
 * @return 这一步返回的等待毫秒数：0 紧接着写；重试时是重试间隔；失败时见 `flash_pressure_fail`。
 */
static uint32_t flash_pressure_step_open(void)
{
    const Service_StatusTypeDef status = Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                                                     FLASH_PRESSURE_PATH,
                                                                     SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                                                     &flash_pressure.file);

    if (status == SERVICE_OK)
    {
        flash_pressure.file_open = true;
        flash_pressure.file_done = false;
        flash_pressure.file_bytes = 0U;
        flash_pressure.busy_retries = 0U;
        flash_pressure.phase = FLASH_PRESSURE_WRITE;
        return 0U;
    }

    if ((status == SERVICE_BUSY) && (flash_pressure.busy_retries < BACKGROUND_HOOK_FLASH_PRESSURE_BUSY_RETRY_MAX))
    {
        flash_pressure.busy_retries++;
        flash_pressure.phase = FLASH_PRESSURE_CLEANUP;
        return BACKGROUND_HOOK_FLASH_PRESSURE_BUSY_RETRY_MS;
    }

    return flash_pressure_fail(FLASH_PRESSURE_OPEN, status);
}

/**
 * @brief  一次回收；失败一次就停。成功后回到写（文件已写满则去关闭），让出一个节拍。
 * @return 这一步返回的等待毫秒数：成功是节拍；失败时见 `flash_pressure_fail`。
 */
static uint32_t flash_pressure_step_reclaim(void)
{
    const TickType_t start = xTaskGetTickCount();
    uint32_t elapsed_ms;
    const Service_StatusTypeDef status = Service_Filesystem_ReclaimFlash();

    elapsed_ms = flash_pressure_elapsed_ms(start);
    if (elapsed_ms > flash_pressure.reclaim_max_ms)
    {
        flash_pressure.reclaim_max_ms = elapsed_ms;
    }

    if (status != SERVICE_OK)
    {
        return flash_pressure_fail(FLASH_PRESSURE_RECLAIM, status);
    }

    flash_pressure.phase = flash_pressure.file_done ? FLASH_PRESSURE_CLOSE : FLASH_PRESSURE_WRITE;
    return BACKGROUND_HOOK_FLASH_PRESSURE_PACE_MS;
}

/**
 * @brief  写一块；卷满（NO_SPACE 或写进去的比要求的少）算这个文件写完，不算故障。
 * @return 这一步返回的等待毫秒数：0 紧接着回收；不回收时是节拍；空闲块低于下限时是节拍。
 */
static uint32_t flash_pressure_step_write(void)
{
    uint32_t request;
    uint32_t done = 0U;
    const uint32_t floor_blocks = BACKGROUND_HOOK_FLASH_PRESSURE_FREE_FLOOR;
    uint32_t elapsed_ms;
    TickType_t start;
    Service_StatusTypeDef status;

    /* 写之前取一份 FTL 诊断快照：失败之后 FTL 不就绪，有效/失效块数读作 0，停下行要用失败前的数字。 */
    flash_pressure.pre_write_valid = (Platform_Flash_GetVolumeDiagnostics(&flash_pressure.pre_write) == PLATFORM_OK);

    /* 空闲块下限（默认 0 不启用）：低于它就不写，只给回收机会，等空闲块回到下限以上。 */
    if ((floor_blocks != 0U) && flash_pressure.pre_write_valid && (flash_pressure.pre_write.FreeBlocks < floor_blocks))
    {
        if (BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM != 0U)
        {
            return flash_pressure_step_reclaim();
        }
        return BACKGROUND_HOOK_FLASH_PRESSURE_PACE_MS;
    }

    request = BACKGROUND_HOOK_FLASH_PRESSURE_FILE_BYTES - flash_pressure.file_bytes;
    if (request > BACKGROUND_HOOK_FLASH_PRESSURE_CHUNK_BYTES)
    {
        request = BACKGROUND_HOOK_FLASH_PRESSURE_CHUNK_BYTES;
    }

    flash_pressure.write_request = request;
    start = xTaskGetTickCount();
    status = Service_Filesystem_WriteFile(flash_pressure.file,
                                          (const void *)BACKGROUND_HOOK_FLASH_PRESSURE_SOURCE_ADDRESS,
                                          request,
                                          &done);
    elapsed_ms = flash_pressure_elapsed_ms(start);
    if (elapsed_ms > flash_pressure.write_max_ms)
    {
        flash_pressure.write_max_ms = elapsed_ms;
    }

    if ((status != SERVICE_OK) && (status != SERVICE_NO_SPACE))
    {
        return flash_pressure_fail(FLASH_PRESSURE_WRITE, status);
    }

    flash_pressure.file_bytes += done;
    flash_pressure.total_bytes += done;
    if ((status == SERVICE_NO_SPACE) || (done < request))
    {
        flash_pressure.file_done = true;
        if (!flash_pressure.full_reported)
        {
            char text[64];

            flash_pressure.full_reported = true;
            (void)snprintf(text, sizeof(text), "flash full after %lu B, closing file", (unsigned long)flash_pressure.file_bytes);
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, flash_pressure_tag, text);
        }
    }
    else if (flash_pressure.file_bytes >= BACKGROUND_HOOK_FLASH_PRESSURE_FILE_BYTES)
    {
        flash_pressure.file_done = true;
    }

    if (BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM != 0U)
    {
        flash_pressure.phase = FLASH_PRESSURE_RECLAIM;
        return 0U; /* 紧接着回收；节拍由回收那一步让出。 */
    }

    flash_pressure.phase = flash_pressure.file_done ? FLASH_PRESSURE_CLOSE : FLASH_PRESSURE_WRITE;
    return BACKGROUND_HOOK_FLASH_PRESSURE_PACE_MS;
}

/**
 * @brief  关闭文件；失败后停下，不再删除。
 * @return 这一步返回的等待毫秒数：0 紧接着删除；停下时是轮询间隔。
 */
static uint32_t flash_pressure_step_close(void)
{
    const Service_StatusTypeDef status = Service_Filesystem_CloseFile(flash_pressure.file);

    flash_pressure.file_open = false;
    flash_pressure.file.Token = 0U;

    if (Service_Filesystem_StatusIsVolumeUnavailable(status))
    {
        return flash_pressure_fail(FLASH_PRESSURE_CLOSE, status);
    }
    if (flash_pressure.stop_after_close)
    {
        flash_pressure.phase = FLASH_PRESSURE_STOPPED;
        return BACKGROUND_HOOK_FLASH_PRESSURE_WAIT_VOLUME_MS;
    }
    if (status != SERVICE_OK)
    {
        return flash_pressure_stop(FLASH_PRESSURE_CLOSE, status);
    }

    flash_pressure.phase = FLASH_PRESSURE_REMOVE;
    return 0U;
}

/**
 * @brief  删除写满的文件（Flash 卷锁里按每 MiB 约 138 ms，4 MiB 约 0.55 s），记最长耗时。
 * @return 这一步返回的等待毫秒数：成功是节拍；失败时见 `flash_pressure_fail`。
 */
static uint32_t flash_pressure_step_remove(void)
{
    const TickType_t start = xTaskGetTickCount();
    uint32_t elapsed_ms;
    const Service_StatusTypeDef status = Service_Filesystem_RemoveFile(SERVICE_FILESYSTEM_VOLUME_FLASH, FLASH_PRESSURE_PATH);

    elapsed_ms = flash_pressure_elapsed_ms(start);
    if (elapsed_ms > flash_pressure.remove_max_ms)
    {
        flash_pressure.remove_max_ms = elapsed_ms;
    }

    if (status != SERVICE_OK)
    {
        return flash_pressure_fail(FLASH_PRESSURE_REMOVE, status);
    }

    flash_pressure.files_done++;
    flash_pressure.phase = FLASH_PRESSURE_OPEN;
    return BACKGROUND_HOOK_FLASH_PRESSURE_PACE_MS;
}

/**
 * @brief  Flash 压力作业的一步。
 * @return 多少毫秒后再调：0 马上；等卷或已停下时是轮询间隔；钩子关闭时是 `BACKGROUND_WAIT_FOREVER`。
 * @note   Flash 卷没挂载（含 Flash 启动任务的 `MountFlash` 3.3 至 8.3 s 期间，那时卷事件还没发）一律不碰 Flash 卷：
 *         `volume_status()` 不拿锁，作业只在卷已就绪之后才调文件接口，所以不会被 `MountFlash` 持的锁挡住。
 *         Flash 卷锁里的单次调用最长：写 32 KiB（空闲块充足时约 0.25 s，接近耗尽时约 1.5 至 3 s）、删除 4 MiB（约 0.55 s）、
 *         回收一块（约 40 至 50 ms）；这些只挡 Flash 卷，SD 卷的锁与此无关（R4.2）。
 */
uint32_t background_flash_pressure_step(void)
{
    Volume_StatusTypeDef volume;
    TickType_t now;

    if (BACKGROUND_HOOK_FLASH_PRESSURE == 0U)
    {
        return BACKGROUND_WAIT_FOREVER;
    }

    volume = volume_status(VOLUME_ID_FLASH);
    if (!volume.Mounted)
    {
        flash_pressure_enter_wait();
        return BACKGROUND_HOOK_FLASH_PRESSURE_WAIT_VOLUME_MS;
    }

    now = xTaskGetTickCount();
    if ((flash_pressure.phase == FLASH_PRESSURE_WAIT_VOLUME) || (volume.Sequence != flash_pressure.volume_sequence))
    {
        flash_pressure_start_session(volume.Sequence, now);
    }
    else if ((flash_pressure.phase != FLASH_PRESSURE_STOPPED) &&
             (flash_pressure_elapsed_ms(flash_pressure.last_log) >= BACKGROUND_HOOK_FLASH_PRESSURE_LOG_PERIOD_MS))
    {
        flash_pressure_post_stats("run", now);
    }

    switch (flash_pressure.phase)
    {
        case FLASH_PRESSURE_CLEANUP:
            return flash_pressure_step_cleanup();
        case FLASH_PRESSURE_OPEN:
            return flash_pressure_step_open();
        case FLASH_PRESSURE_WRITE:
            return flash_pressure_step_write();
        case FLASH_PRESSURE_RECLAIM:
            return flash_pressure_step_reclaim();
        case FLASH_PRESSURE_CLOSE:
            return flash_pressure_step_close();
        case FLASH_PRESSURE_REMOVE:
            return flash_pressure_step_remove();
        case FLASH_PRESSURE_WAIT_VOLUME:
        case FLASH_PRESSURE_STOPPED:
        default:
            return BACKGROUND_HOOK_FLASH_PRESSURE_WAIT_VOLUME_MS;
    }
}

/**
 * @brief  钩子打开时投启动日志：开了什么、怎么调的（DIAG-7：没有这一行就是没开）。
 */
void background_flash_pressure_announce(void)
{
    char text[128];

    if (BACKGROUND_HOOK_FLASH_PRESSURE != 0U)
    {
        (void)snprintf(text,
                       sizeof(text),
                       "flash_pressure ON chunk=%lu file=%lu pace=%lums floor=%lu reclaim=%lu log=%lums",
                       (unsigned long)BACKGROUND_HOOK_FLASH_PRESSURE_CHUNK_BYTES,
                       (unsigned long)BACKGROUND_HOOK_FLASH_PRESSURE_FILE_BYTES,
                       (unsigned long)BACKGROUND_HOOK_FLASH_PRESSURE_PACE_MS,
                       (unsigned long)BACKGROUND_HOOK_FLASH_PRESSURE_FREE_FLOOR,
                       (unsigned long)BACKGROUND_HOOK_FLASH_PRESSURE_RECLAIM,
                       (unsigned long)BACKGROUND_HOOK_FLASH_PRESSURE_LOG_PERIOD_MS);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, flash_pressure_hook_tag, text);
    }
}
