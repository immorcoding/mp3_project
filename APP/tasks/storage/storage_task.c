/**
 * @file storage_task.c
 * @brief Storage Task 入口：启动 SD/Flash 编排并调度热插拔消抖与空闲回收。
 */

#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/storage_task_config.h"
#include "APP/tasks/storage/flash/storage_flash.h"
#include "APP/tasks/storage/sd/storage_sd.h"
#include "APP/tasks/storage/benchmark/storage_sdram_benchmark.h"
#include "APP/tasks/storage/benchmark/storage_sdram_benchmark_config.h"
#include "catalog/storage_listbuffer.h"

#include <stdint.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include "Service/log/log_service.h"

typedef enum
{
    STORAGE_TASK_SD_NOT_READY = 0U,
    STORAGE_TASK_SD_DEBOUNCING,
    STORAGE_TASK_SD_READY
} Storage_TaskSdStateTypeDef;

static volatile Storage_TaskSdStateTypeDef storage_task_sd_state =
    STORAGE_TASK_SD_NOT_READY;

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
 * @brief  按 SD 是否已挂载设为就绪或未就绪。
 * @note   仅在启动后与消抖 process 完成后调用。未就绪时收掉卡住的 PENDING。
 */
static void storage_task_apply_sd_state(void)
{
    if (storage_sd_volume_is_mounted())
    {
        storage_task_sd_state = STORAGE_TASK_SD_READY;
    }
    else
    {
        storage_task_sd_state = STORAGE_TASK_SD_NOT_READY;
        storage_listbuffer_complete_unavailable();
    }
}

/**
 * @brief  进入消抖中并重开静默起点。
 * @param[in] now 当前 tick，作为新的消抖 start。
 * @note   已就绪时也立刻离开就绪，禁止新的窗口请求。
 */
static void storage_task_enter_debounce(TickType_t now, TickType_t *debounce_start)
{
    storage_task_sd_state = STORAGE_TASK_SD_DEBOUNCING;
    *debounce_start = now;
}

/**
 * @brief  本圈 wait 应阻塞多久：回收剩余与（若在消抖中）消抖剩余的较小值。
 * @param[in] now 当前 tick。
 * @param[in] debounce_start 消抖起点。
 * @param[in] reclaim_start 回收起点。
 * @return 传给通知等待的超时。
 */
static TickType_t storage_task_next_wait_ticks(TickType_t now,
                                               TickType_t debounce_start,
                                               TickType_t reclaim_start)
{
    TickType_t wait_ticks;

    wait_ticks = storage_task_ticks_until(now,
                                          reclaim_start,
                                          pdMS_TO_TICKS(STORAGE_FLASH_RECLAIM_PERIOD_MS));

    if (storage_task_sd_state == STORAGE_TASK_SD_DEBOUNCING)
    {
        TickType_t debounce_wait;

        debounce_wait = storage_task_ticks_until(now,
                                                 debounce_start,
                                                 pdMS_TO_TICKS(STORAGE_SD_DEBOUNCE_MS));
        if (debounce_wait < wait_ticks)
        {
            wait_ticks = debounce_wait;
        }
    }

    return wait_ticks;
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
 * @brief  仅 SD 就绪时若有 PENDING 则填 Queue 窗口。
 * @note   仅 Storage Task 普通上下文可调用。消抖中与未就绪都不填窗。
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
 * @return true SD FatFs 卷已挂载。
 * @note   GUI Task 可调用；ISR 不可调用。消抖中与未就绪均为 false。不表示曲库非空。
 */
bool storage_task_sd_is_ready(void)
{
    return storage_task_sd_state == STORAGE_TASK_SD_READY;
}

/**
 * @brief  SD FatFs 卷是否仍挂载。
 * @return true 卷对象仍在；消抖中只要尚未卸载也为 true。
 * @note   GUI Task 可调用；ISR 不可调用。与 `storage_task_sd_is_ready()` 不同：
 *         后者在消抖中为 false，只表示此时不能 request，不表示卡已拔走。
 */
bool storage_task_sd_is_mounted(void)
{
    return storage_sd_volume_is_mounted();
}

/**
 * @brief  运行 Storage Task 的存储协调与 SD 卡热插拔调度循环。
 * @param  handle 当前未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
 * @note   SDRAM 破坏性自检必须早于 Flash 挂载使用的 FTL 表。随后 Flash 完成
 *         执行器绑定、可选物理基准和挂载策略，再初始化 SD 热插拔。
 *
 *         主循环维护就绪 / 消抖中 / 未就绪。检测 FLAG 立刻进消抖中并重开
 *         30 ms 起点；wait 超时取消抖剩余与 100 ms 回收剩余的较小值。
 *         消抖截止到点后才 storage_sd_process()，再按 SD 是否已挂载回到就绪或未就绪。
 *         仅就绪时接受窗口 request 并 load。
 *
 *         STORAGE_NOTIFY_SD_TRANSFER 与 STORAGE_NOTIFY_FLASH_OPERATION 由同一任务
 *         调用栈中的 Filesystem SD/Flash 私有执行器等待。
 *         它们完成后返回各自调用者，绝不在 IRQ 中提交下一笔传输。
 */
void storage_task(void *handle)
{
    TaskHandle_t task_handle;
    TickType_t debounce_start = 0U;
    TickType_t reclaim_start;

    (void)handle;
    task_handle = xTaskGetCurrentTaskHandle();
    storage_listbuffer_bind(task_handle);

    _Static_assert((unsigned)STORAGE_NOTIFY_COUNT <=
                       (unsigned)configTASK_NOTIFICATION_ARRAY_ENTRIES,
                   "Storage Task notify slots exceed FreeRTOS array length");

#if STORAGE_SDRAM_BENCHMARK_ENABLE
    storage_sdram_benchmark_run();
#endif

    if (storage_flash_init(task_handle) != STORAGE_OK)
    {
        /* Handle flash initialization error */
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "STORAGE",
                               "Flash initialization failed.");
    }
    if (storage_sd_init(task_handle) != STORAGE_OK)
    {
        /* Handle SD initialization error */
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               "STORAGE",
                               "SD initialization failed.");
    }

    storage_task_apply_sd_state();
    reclaim_start = xTaskGetTickCount();

    for (;;)
    {
        TickType_t now;
        uint32_t flags;

        now = xTaskGetTickCount();
        flags = storage_task_wait_event(
            storage_task_next_wait_ticks(now, debounce_start, reclaim_start));
        now = xTaskGetTickCount();

        if ((flags & STORAGE_NOTIFY_FLAG_SD_DETECT) != 0U)
        {
            storage_task_enter_debounce(now, &debounce_start);
        }

        if ((storage_task_sd_state == STORAGE_TASK_SD_DEBOUNCING) &&
            (storage_task_ticks_until(now,
                                      debounce_start,
                                      pdMS_TO_TICKS(STORAGE_SD_DEBOUNCE_MS)) == 0U))
        {
            if (storage_sd_process() != STORAGE_OK)
            {
                (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                       "STORAGE",
                                       "SD process failed.");
            }
            storage_task_apply_sd_state();
        }

        storage_task_load_listbuffer_if_allowed();

        now = xTaskGetTickCount();
        if (storage_task_ticks_until(now,
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
