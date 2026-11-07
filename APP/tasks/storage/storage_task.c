/**
 * @file storage_task.c
 * @brief Storage Task 入口：卷事件的订阅者，只剩曲库扫描和 Flash 回收（音频流在音频读取任务，曲名与 Queue 窗口在前台任务，#215、#218）。
 *
 * @details
 *          热插拔、挂载和卸载已经归卷任务（`APP/tasks/volume/`）：本任务不再碰 Platform SD，也不消抖。
 *          它在入口登记成卷事件的订阅者，被叫醒后比对 SD 卷序号：序号变了，旧的全部作废（曲库），
 *          再看现状，卷挂着就扫描曲库；扫描发布后曲库叫醒前台任务一次（`catalog.c`）。音频流的 `CARD_REMOVED` 由音频读取任务
 *          自己写，曲名与窗口行由前台任务读，不在这里。优先级 1（#218：GUI、Log、Monitor、前台任务都升到 2）：扫描只拿空隙，
 *          不抢前台；扫描和回收随后台任务票（#220）搬进后台任务。
 */

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/storage_task_config.h"
#include "APP/tasks/storage/flash/storage_flash.h"
#include "APP/apps/music/catalog/catalog.h"
#include "APP/tasks/volume/volume_event.h"

#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "Service/log/log_service.h"

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
 *         作废曲库。音频流的 `CARD_REMOVED` 由音频读取任务自己收尾（它也订阅了卷事件），与本任务不分先后：
 *         曲库代次在卷序号变了之后就读作 0（`catalog_read_track()`），所以不会有人按旧曲库开新流；前台任务也订阅了卷事件，
 *         代次读作 0 时发布代次 0 的空窗，曲名答代次 0。卷任务不等本任务：卸载已使旧令牌失效，晚一点收尾也碰不到旧卡。
 *         换卡时序号跳 2，同样走这里：旧卡的流和曲库作废，再对新卡扫描。扫描完成发布（代次非 0）后曲库叫醒前台任务一次。
 *         扫描被丢弃（卷不可用、扫描途中卷序号又变了）：卷变了就等下一个卷事件，不会把空库当成已就绪；
 *         扫描被丢弃但序号没变（卡还在、读不了）则记 ERROR，曲库保持代次 0，拔插卡后重扫。
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

    (void)catalog_invalidate();

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
         * 曲库保持代次 0（前台任务据此发空窗）；拔插卡（边沿）后重新对账和重扫。 */
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "STORAGE",
                               "Catalog scan aborted but the card is still there; reinsert the card.");
    }
}

/**
 * @brief  本圈 wait 应阻塞多久。
 * @param[in] now 当前 tick。
 * @param[in] reclaim_start 回收起点。
 * @return 传给通知等待的超时。
 * @note   Flash 卷还没挂载（Flash 启动任务的 `MountFlash` 在持锁）时不回收，
 *         回收剩余不参与计时，只靠通知（含 Flash 卷就绪的卷事件）唤醒。打开的音频流不再影响回收机会（STOR-12：
 *         音频读取和回收分到两个任务、SD 与 Flash 各一把锁之后，擦除不再挡补块）。
 */
static TickType_t storage_task_next_wait_ticks(TickType_t now, TickType_t reclaim_start)
{
    if (!volume_status(VOLUME_ID_FLASH).Mounted)
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
 * @brief  运行 Storage Task：卷事件的订阅者，加曲库扫描和 Flash 回收。
 * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
 * @note   块池和音频信箱已由 Boot 任务初始化、归音频读取任务（#215）；SDRAM 破坏性自检也在 Boot 任务里、
 *         创建本任务之前运行完毕（HWD-2）。入口登记卷事件订阅并先比对一次序号：本任务比卷任务晚启动也不会漏。
 *
 *         每圈顺序：卷事件（扫描）→ 回收。曲名与 Queue 窗口行归前台任务（#218），本任务不再有窗口槽和名称槽。
 *         文件调用在 Filesystem 入口等闸（消抖期间）或等 SD 卷锁，所以本任务不知道也不需要知道消抖。
 */
void storage_task(void *handle)
{
    TaskHandle_t task_handle;
    TickType_t reclaim_start;

    (void)handle;
    task_handle = xTaskGetCurrentTaskHandle();

    _Static_assert((unsigned)STORAGE_NOTIFY_COUNT <=
                       (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
                   "Storage Task notify slots exceed FreeRTOS array length");

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

        now = xTaskGetTickCount();
        if (!volume_status(VOLUME_ID_FLASH).Mounted)
        {
            reclaim_start = now; /* Flash 卷就绪之前不回收；就绪后从那一刻起算一个周期。 */
        }
        else if (storage_task_ticks_until(now,
                                          reclaim_start,
                                          pdMS_TO_TICKS(STORAGE_FLASH_RECLAIM_PERIOD_MS)) == 0U)
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
