/**
 * @file storage_task.c
 * @brief Storage Task 入口：卷事件的订阅者，继续做音频流、曲库扫描、两个槽和 Flash 回收。
 *
 * @details
 *          热插拔、挂载和卸载已经归卷任务（`APP/tasks/volume/`）：本任务不再碰 Platform SD，也不消抖。
 *          它在入口登记成卷事件的订阅者，被叫醒后比对 SD 卷序号：序号变了，旧的全部作废（`CARD_REMOVED`、
 *          曲库、两个槽），再看现状，卷挂着就扫描曲库。
 */

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/storage_task_config.h"
#include "APP/tasks/storage/flash/storage_flash.h"
#include "APP/apps/music/catalog/catalog.h"
#include "APP/tasks/volume/volume_event.h"
#include "catalog/storage_listbuffer.h"
#include "catalog/storage_namebuffer.h"
#include "stream/storage_audio_block.h"
#include "stream/storage_audio_stream.h"

#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "Service/log/log_service.h"

typedef enum
{
    STORAGE_TASK_SD_NOT_READY = 0U,
    STORAGE_TASK_SD_READY
} Storage_TaskSdStateTypeDef;

/** @brief 本任务对 SD 的看法：卷挂着且曲库扫描完成才是就绪。扫描期间、卷消失后都是未就绪。 */
static volatile Storage_TaskSdStateTypeDef storage_task_sd_state =
    STORAGE_TASK_SD_NOT_READY;

/** @brief 已经处理过的 SD 卷序号；只由 Storage Task 访问。 */
static uint32_t storage_task_sd_sequence_seen;

/**
 * @brief  计算从 start 到 now 经过的 tick，无符号减法自动处理回绕。
 * @param[in] now 当前 tick。
 * @param[in] start 起点 tick。
 * @return 经过的 tick 数。
 */
static TickType_t storage_task_ticks_since(TickType_t now, TickType_t start)
{
    return now - start;
}

/**
 * @brief  距 period 到期还剩多少 tick；已到期则为 0。
 * @param[in] now 当前 tick。
 * @param[in] start 本周期起点。
 * @param[in] period 周期长度。
 * @return 剩余 tick；已到点为 0。
 */
static TickType_t storage_task_ticks_until(TickType_t now,
                                           TickType_t start,
                                           TickType_t period)
{
    TickType_t elapsed;

    elapsed = storage_task_ticks_since(now, start);
    if (elapsed >= period)
    {
        return 0U;
    }

    return period - elapsed;
}

/**
 * @brief  处理 SD 卷事件：序号变了，旧的全部作废，再看现状；卷挂着就扫描曲库。
 * @note   每圈都调用（比对一个字，没变就立刻返回），所以丢叫醒、合并叫醒、晚订阅都不会漏。
 *         作废的顺序沿用原来的拔卡路径：先写 `CARD_REMOVED`（当前流仍被播放任务持有就写，已读完的流也算），
 *         再作废曲库，收掉两个槽。卷任务不等本任务：卸载已使旧令牌失效，晚一点收尾也碰不到旧卡。
 *         换卡时序号跳 2，同样走这里：旧卡的流和曲库作废，再对新卡扫描。
 *         扫描期间 `storage_task_sd_is_ready()` 为假；扫描完成后若序号又变了（含扫描被卷不可用丢弃、卡已拔走），
 *         不置就绪，留给下一圈处理，不会把空库置就绪等事件；扫描被丢弃但序号没变（卡还在、读不了）则记 ERROR，
 *         置就绪、曲库为空。
 */
static void storage_task_apply_sd_volume(void)
{
    Volume_StatusTypeDef volume = volume_status(VOLUME_ID_SD);
    bool unchanged;

    if (volume.Sequence == storage_task_sd_sequence_seen)
    {
        return;
    }

    storage_task_sd_sequence_seen = volume.Sequence;
    storage_task_sd_state = STORAGE_TASK_SD_NOT_READY;

    storage_audio_stream_card_removed();
    (void)catalog_invalidate();
    storage_listbuffer_complete_unavailable();
    storage_namebuffer_complete_unavailable();

    if (!volume.Mounted)
    {
        return;
    }

    if (catalog_init() != CATALOG_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "STORAGE",
                               "Catalog initialization failed.");
    }

    volume = volume_status(VOLUME_ID_SD);
    unchanged = volume.Mounted && (volume.Sequence == storage_task_sd_sequence_seen);
    if (catalog_scan_verdict(unchanged) == CATALOG_SCAN_UNREADABLE)
    {
        /* 扫描被卷不可用丢弃，但卷没变：卡还在、读不了（设备出错），没有边沿就没有卷事件，不能等。
         * 沿用原来扫描失败的做法：卷挂着就绪、曲库为空；拔插卡（边沿）后重新对账和重扫。 */
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "STORAGE",
                               "Catalog scan aborted but the card is still there; reinsert the card.");
    }
    if (unchanged)
    {
        storage_task_sd_state = STORAGE_TASK_SD_READY;
    }
}

/**
 * @brief  当前 SD 状态对音频流的介质视图。
 * @return 就绪 / 未挂载或扫描中。
 */
static Storage_AudioMediaTypeDef storage_task_audio_media(void)
{
    return (storage_task_sd_state == STORAGE_TASK_SD_READY) ? STORAGE_AUDIO_MEDIA_READY
                                                            : STORAGE_AUDIO_MEDIA_ABSENT;
}

/**
 * @brief  本圈 wait 应阻塞多久。
 * @param[in] now 当前 tick。
 * @param[in] reclaim_start 回收起点。
 * @return 传给通知等待的超时。
 * @note   音频流还有工作（待处理请求，或流已打开且有空闲块可读）、或就绪时 Queue 窗口仍在逐行填写时不阻塞。
 *         有打开的音频流时不提供回收机会；Flash 卷还没挂载（Flash 启动任务的 `MountFlash` 在持锁）时也不回收，
 *         这两种情况下回收剩余都不参与计时，只靠通知（含 Flash 卷就绪的卷事件）唤醒。
 */
static TickType_t storage_task_next_wait_ticks(TickType_t now, TickType_t reclaim_start)
{
    if (storage_audio_stream_has_work())
    {
        return 0U;
    }

    /* 窗口逐圈读一行标签（STOR-16）：填到一半时下一圈接着读，不阻塞等事件。 */
    if ((storage_task_sd_state == STORAGE_TASK_SD_READY) &&
        (storage_listbuffer.Status == STORAGE_LISTBUFFER_PENDING))
    {
        return 0U;
    }

    if (storage_audio_stream_is_open() || !volume_status(VOLUME_ID_FLASH).Mounted)
    {
        return portMAX_DELAY;
    }

    return storage_task_ticks_until(now,
                                    reclaim_start,
                                    pdMS_TO_TICKS(STORAGE_FLASH_RECLAIM_PERIOD_MS));
}

/**
 * @brief  有界等待主循环事件槽，成功时一次收取并清除全部事件 FLAG。
 * @param[in] ticks 最长阻塞时间；0 表示不等待，已有通知仍会收取并清 FLAG。
 * @return 本次收取的 FLAG；超时或无通知时为 0。
 * @note   仅 Storage Task 普通上下文可调用。
 */
static uint32_t storage_task_wait_event(TickType_t ticks)
{
    uint32_t flags = 0U;

    if (xTaskNotifyWaitIndexed((UBaseType_t)STORAGE_NOTIFY_EVENT,
                               0U,
                               STORAGE_NOTIFY_FLAG_EVENT_MASK,
                               &flags,
                               ticks) != pdTRUE)
    {
        return 0U;
    }

    return flags;
}

/**
 * @brief  仅 SD 就绪时若有 PENDING 则为 Queue 窗口读一行曲目标签，整窗填完才 READY。
 * @note   仅 Storage Task 普通上下文可调用，每圈一次。未就绪时不填窗：已填的行保留、恢复就绪后按代次续填
 *         或以空窗收尾；卷事件处理时由 complete_unavailable() 收掉。
 */
static void storage_task_load_listbuffer_if_allowed(void)
{
    if (storage_task_sd_state != STORAGE_TASK_SD_READY)
    {
        return;
    }

    if (storage_listbuffer_load() != STORAGE_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "STORAGE",
                               "List buffer load failed.");
    }
}

/**
 * @brief  GUI 是否可发起 Queue 窗口请求。
 * @return true SD 卷已挂载且本任务已处理完这次挂载（曲库扫描完成）。
 * @note   GUI Task 可调用；ISR 不可调用。扫描中与未挂载均为 false。不表示曲库非空。
 *         消抖不再体现在这里：消抖期间调用在 Filesystem 入口等闸（#195），所以这里不再有“消抖中”。
 */
bool storage_task_sd_is_ready(void)
{
    return storage_task_sd_state == STORAGE_TASK_SD_READY;
}

/**
 * @brief  SD FatFs 卷是否仍挂载（卷事件的挂载位，读一个字，不拿锁）。
 * @return true 卷挂着；扫描中也为 true。
 * @note   GUI Task 可调用；ISR 不可调用。与 `storage_task_sd_is_ready()` 不同：
 *         后者在扫描中为 false，只表示此时不能 request，不表示卡已拔走。
 */
bool storage_task_sd_is_mounted(void)
{
    return volume_status(VOLUME_ID_SD).Mounted;
}

/**
 * @brief  运行 Storage Task：卷事件的订阅者，加音频流、曲库扫描、两个槽和 Flash 回收。
 * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
 * @note   块池和信箱在这里初始化（随音频读取任务搬走前仍归本任务）；SDRAM 破坏性自检已在 Boot 任务里、
 *         创建本任务之前运行完毕（HWD-2）。入口登记卷事件订阅并先比对一次序号：本任务比卷任务晚启动也不会漏。
 *
 *         每圈顺序：卷事件 → 请求信箱 → 列表窗口（至多一行标签）→ 曲目显示名 → 最多读一块 → 回收。
 *         文件调用在 Filesystem 入口等闸（消抖期间）或等 SD 卷锁，所以本任务不知道也不需要知道消抖。
 */
void storage_task(void *handle)
{
    TaskHandle_t task_handle;
    TickType_t reclaim_start;

    (void)handle;
    task_handle = xTaskGetCurrentTaskHandle();
    storage_listbuffer_bind(task_handle);
    storage_namebuffer_bind(task_handle);

    _Static_assert((unsigned)STORAGE_NOTIFY_COUNT <=
                       (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
                   "Storage Task notify slots exceed FreeRTOS array length");

    /* 块池 NOLOAD 段的内容此时才开始有主；必须晚于可选的 SDRAM 全容量诊断（HWD-2，诊断在 Boot 任务里）。 */
    storage_audio_block_init();
    storage_audio_stream_init(task_handle);

    if (!volume_subscribe(task_handle, STORAGE_NOTIFY_EVENT, STORAGE_NOTIFY_FLAG_VOLUME))
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "STORAGE",
                               "Volume event subscription failed.");
    }
    storage_task_apply_sd_volume();
    reclaim_start = xTaskGetTickCount();

    for (;;)
    {
        TickType_t now;

        now = xTaskGetTickCount();
        (void)storage_task_wait_event(storage_task_next_wait_ticks(now, reclaim_start));

        storage_task_apply_sd_volume();

        storage_audio_stream_process_request(storage_task_audio_media());
        storage_task_load_listbuffer_if_allowed();
        storage_namebuffer_load(storage_task_sd_state == STORAGE_TASK_SD_READY);
        storage_audio_stream_read_block(storage_task_audio_media(), storage_task_sd_sequence_seen);

        now = xTaskGetTickCount();
        if (!volume_status(VOLUME_ID_FLASH).Mounted)
        {
            reclaim_start = now; /* Flash 卷就绪之前不回收；就绪后从那一刻起算一个周期。 */
        }
        else if (!storage_audio_stream_is_open() &&
                 (storage_task_ticks_until(now,
                                           reclaim_start,
                                           pdMS_TO_TICKS(STORAGE_FLASH_RECLAIM_PERIOD_MS)) == 0U))
        {
            if (storage_flash_reclaim() != STORAGE_OK)
            {
                (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                       "STORAGE",
                                       "Flash reclaim failed.");
            }
            reclaim_start = now;
        }
    }
}
