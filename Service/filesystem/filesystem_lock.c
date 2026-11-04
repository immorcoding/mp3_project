/**
 * @file filesystem_lock.c
 * @brief Filesystem Module 私有锁与锁观测（spike，#191、#199）。
 *
 * @details
 *          锁用静态分配的 FreeRTOS 互斥量（带优先级继承），在 Boot Task 里经
 *          `Service_Filesystem_SpikeInit()`（在 filesystem_service.c）先建好，任何任务用到时也会幂等补建。统计在短临界区内更新，
 *          只记极值和计数，不记逐次日志（DIAG-3）。
 */

#include "Service/filesystem/filesystem_lock.h"
#include "Service/filesystem/filesystem_spike.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/log/log_service.h"

#if FILESYSTEM_LOCK_PER_VOLUME
#define FILESYSTEM_LOCK_COUNT 4U        /* SD、Flash、打开表锁、槽表锁。 */
#define FILESYSTEM_LOCK_FLASH_INDEX 1U
#define FILESYSTEM_LOCK_TABLE_INDEX 2U
#define FILESYSTEM_LOCK_SLOT_INDEX 3U
#define FILESYSTEM_LOCK_VOLUME_COUNT 2U /* 前两把是卷锁，只有它们记“最近一次公开调用的持锁时长”。 */
#else
#define FILESYSTEM_LOCK_COUNT 1U        /* 全局一把。 */
#define FILESYSTEM_LOCK_VOLUME_COUNT 1U
#endif

/** @brief 超过这个 tick 数的区间不再信 32 位 DWT（4 s，远小于 480 MHz 下约 8.9 s 的回绕）。 */
#define FILESYSTEM_LOCK_DWT_LIMIT_TICKS 4000U

/** @brief 记录最近一次持锁时长的任务数；超出时覆盖第 0 项。 */
#define FILESYSTEM_LOCK_LAST_HOLD_SLOTS 8U

_Static_assert(configTICK_RATE_HZ == 1000U, "filesystem_lock tick 兜底按 1 kHz 换算");
_Static_assert(FILESYSTEM_LOCK_COUNT <= SERVICE_FILESYSTEM_SPIKE_LOCK_MAX, "锁数量超过观测接口上限");

/** @brief 最后一个操作条目留给“用尽后的其余操作”。 */
#define FILESYSTEM_LOCK_OTHER_OPERATION "(other)"

/** @brief 起点时间戳：周期数加 tick，用于在 DWT 回绕前兜底。 */
typedef struct
{
    uint32_t Cycles;
    TickType_t Tick;
} filesystem_lock_stamp_t;

/** @brief 一把锁及其统计。 */
typedef struct
{
    SemaphoreHandle_t Handle;
    StaticSemaphore_t Storage;
    TaskHandle_t Holder;
    const char *HolderOperation;
    char HolderName[SERVICE_FILESYSTEM_SPIKE_NAME_BYTES];
    filesystem_lock_stamp_t HoldStart;
    uint32_t WindowEpoch;
    Service_Filesystem_SpikeLockStatsTypeDef Window;
    Service_Filesystem_SpikeLockStatsTypeDef Total;
    Service_Filesystem_SpikeOpStatsTypeDef OpWindow[SERVICE_FILESYSTEM_SPIKE_OP_MAX];
    Service_Filesystem_SpikeOpStatsTypeDef OpTotal[SERVICE_FILESYSTEM_SPIKE_OP_MAX];
} filesystem_lock_t;

/** @brief 每个任务最近一次公开调用的持锁时长。 */
typedef struct
{
    TaskHandle_t Task;
    uint32_t HoldUs;
} filesystem_lock_last_hold_t;

static filesystem_lock_t filesystem_locks[FILESYSTEM_LOCK_COUNT];
static volatile bool filesystem_lock_initialized;
static volatile uint32_t filesystem_lock_window_epoch;
static filesystem_lock_last_hold_t filesystem_lock_last_holds[FILESYSTEM_LOCK_LAST_HOLD_SLOTS];
static volatile uint32_t filesystem_lock_forbidden_calls;
static volatile bool filesystem_lock_forbidden_logged;

/**
 * @brief 取当前时间戳。
 * @return 周期数与 tick 的组合。
 */
static filesystem_lock_stamp_t filesystem_lock_stamp_now(void)
{
    filesystem_lock_stamp_t stamp;

    stamp.Tick = xTaskGetTickCount();
    stamp.Cycles = CortexM7CycleCounter_Read();
    return stamp;
}

/**
 * @brief 计算从 start 到现在的微秒数。
 * @param[in] start 起点时间戳。
 * @return 微秒；区间不足 4 s 且 DWT 可用时按周期数，否则按 tick 取整到毫秒。
 */
static uint32_t filesystem_lock_elapsed_us(const filesystem_lock_stamp_t *start)
{
    const TickType_t tick_delta = xTaskGetTickCount() - start->Tick;
    const uint32_t cycles_per_us = CortexM7CycleCounter_GetFrequencyHz() / 1000000U;

    if ((cycles_per_us == 0U) || (tick_delta >= FILESYSTEM_LOCK_DWT_LIMIT_TICKS))
    {
        return (uint32_t)tick_delta * (1000000U / (uint32_t)configTICK_RATE_HZ);
    }

    return (CortexM7CycleCounter_Read() - start->Cycles) / cycles_per_us;
}

/**
 * @brief 把字符串拷进固定长度的名字缓冲，保证以 NUL 结尾。
 * @param[out] destination 至少 SERVICE_FILESYSTEM_SPIKE_NAME_BYTES 字节。
 * @param[in] source 源字符串，可为 NULL（写成空串）。
 */
static void filesystem_lock_copy_name(char *destination, const char *source)
{
    size_t length = 0U;

    if (source != NULL)
    {
        length = strlen(source);
        if (length >= SERVICE_FILESYSTEM_SPIKE_NAME_BYTES)
        {
            length = SERVICE_FILESYSTEM_SPIKE_NAME_BYTES - 1U;
        }
        memcpy(destination, source, length);
    }
    destination[length] = '\0';
}

/**
 * @brief 在临界区内让一把锁的窗口统计跟上最新的窗口代次。
 * @param[in,out] lock 要同步的锁。
 * @note  调用者已在临界区内；窗口被重置过就清零窗口统计。
 */
static void filesystem_lock_sync_window(filesystem_lock_t *lock)
{
    if (lock->WindowEpoch != filesystem_lock_window_epoch)
    {
        memset(&lock->Window, 0, sizeof(lock->Window));
        memset(lock->OpWindow, 0, sizeof(lock->OpWindow));
        lock->WindowEpoch = filesystem_lock_window_epoch;
    }
}

/**
 * @brief 幂等地创建全部静态互斥量。
 * @note  第一次调用在临界区内创建，之后只读一个标志。可在任何任务启动前调用。
 */
void filesystem_lock_init(void)
{
    uint32_t index;

    if (filesystem_lock_initialized)
    {
        return;
    }

    taskENTER_CRITICAL();
    if (!filesystem_lock_initialized)
    {
        for (index = 0U; index < FILESYSTEM_LOCK_COUNT; index++)
        {
            filesystem_locks[index].Handle = xSemaphoreCreateMutexStatic(&filesystem_locks[index].Storage);
            configASSERT(filesystem_locks[index].Handle != NULL);
        }
        filesystem_lock_initialized = true;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief 记录 GUI 或播放任务调用文件接口的情况：计数，并只记一次 WARN。
 * @param[in] task_name 调用者任务名。
 * @param[in] operation 被调用的公开操作名。
 * @note  不停机：这两个任务按架构不该碰文件接口，但 spike 要让板测能看到而不是把它们卡死。
 */
static void filesystem_lock_check_caller(const char *task_name, const char *operation)
{
    bool first = false;

    if ((strcmp(task_name, "GUI Task") != 0) && (strcmp(task_name, "Playback Task") != 0))
    {
        return;
    }

    taskENTER_CRITICAL();
    filesystem_lock_forbidden_calls++;
    if (!filesystem_lock_forbidden_logged)
    {
        filesystem_lock_forbidden_logged = true;
        first = true;
    }
    taskEXIT_CRITICAL();

    if (first)
    {
        char text[64];

        (void)snprintf(text, sizeof(text), "%s called filesystem op=%s", task_name, operation);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, "SPIKE", text);
    }
}

/**
 * @brief 更新一份统计里的最长等锁。
 * @param[in,out] stats 要更新的统计。
 * @param[in] wait_us 本次等锁微秒。
 * @param[in] task_name 等锁的任务名。
 * @param[in] operation 等锁的操作名。
 * @param[in] blocker_task 等锁开始时的持锁者任务名，可为空串。
 * @param[in] blocker_operation 等锁开始时的持锁者操作名，可为 NULL。
 */
static void filesystem_lock_update_wait(Service_Filesystem_SpikeLockStatsTypeDef *stats,
                                        uint32_t wait_us,
                                        const char *task_name,
                                        const char *operation,
                                        const char *blocker_task,
                                        const char *blocker_operation)
{
    stats->AcquireCount++;
    if (wait_us > stats->WaitMaxUs)
    {
        stats->WaitMaxUs = wait_us;
        filesystem_lock_copy_name(stats->WaitTask, task_name);
        stats->WaitOperation = operation;
        filesystem_lock_copy_name(stats->BlockerTask, blocker_task);
        stats->BlockerOperation = blocker_operation;
    }
}

/**
 * @brief 更新一份统计里的最长持锁。
 * @param[in,out] stats 要更新的统计。
 * @param[in] hold_us 本次持锁微秒。
 * @param[in] task_name 持锁的任务名。
 * @param[in] operation 持锁的操作名。
 */
static void filesystem_lock_update_hold(Service_Filesystem_SpikeLockStatsTypeDef *stats,
                                        uint32_t hold_us,
                                        const char *task_name,
                                        const char *operation)
{
    if (hold_us > stats->HoldMaxUs)
    {
        stats->HoldMaxUs = hold_us;
        filesystem_lock_copy_name(stats->HoldTask, task_name);
        stats->HoldOperation = operation;
    }
}

/**
 * @brief 在一张按操作分开的表里记一次持有。
 * @param[in,out] table 长度 SERVICE_FILESYSTEM_SPIKE_OP_MAX 的表。
 * @param[in] hold_us 本次持锁微秒。
 * @param[in] operation 公开操作名（静态字符串）。
 * @note  先按指针找（同一个字面量），再按内容找（编译器没有合并字面量时），都没有就占一个空位；
 *        只剩最后一项时并入“(other)”。调用者已在临界区内，所以这里只做短的比较。
 */
static void filesystem_lock_update_operation(Service_Filesystem_SpikeOpStatsTypeDef *table,
                                             uint32_t hold_us,
                                             const char *operation)
{
    Service_Filesystem_SpikeOpStatsTypeDef *entry = NULL;
    uint32_t index;

    if (operation == NULL)
    {
        return;
    }
    for (index = 0U; index < SERVICE_FILESYSTEM_SPIKE_OP_MAX; index++)
    {
        if (table[index].Operation == operation)
        {
            entry = &table[index];
            break;
        }
    }
    for (index = 0U; (entry == NULL) && (index < SERVICE_FILESYSTEM_SPIKE_OP_MAX); index++)
    {
        if (table[index].Operation == NULL)
        {
            break;
        }
        if (strcmp(table[index].Operation, operation) == 0)
        {
            entry = &table[index];
        }
    }
    if (entry == NULL)
    {
        const bool last = (index >= (SERVICE_FILESYSTEM_SPIKE_OP_MAX - 1U));

        entry = &table[last ? (SERVICE_FILESYSTEM_SPIKE_OP_MAX - 1U) : index];
        if (entry->Operation == NULL)
        {
            entry->Operation = last ? FILESYSTEM_LOCK_OTHER_OPERATION : operation;
        }
    }
    entry->Count++;
    if (hold_us > entry->HoldMaxUs)
    {
        entry->HoldMaxUs = hold_us;
    }
}

/**
 * @brief 拿一把锁，等到拿到为止，并记录等锁与持锁起点。
 * @param[in,out] lock 要拿的锁。
 * @param[in] operation 公开操作名（静态字符串）。
 * @note  同一任务重复拿同一把锁会永远等下去，这里用断言挡住（`configUSE_RECURSIVE_MUTEXES` 为 0）。
 */
static void filesystem_lock_take(filesystem_lock_t *lock, const char *operation)
{
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    const char *task_name = pcTaskGetName(NULL);
    char blocker_task[SERVICE_FILESYSTEM_SPIKE_NAME_BYTES];
    const char *blocker_operation;
    filesystem_lock_stamp_t wait_start;
    uint32_t wait_us;

    filesystem_lock_init();
    filesystem_lock_check_caller(task_name, operation);
    configASSERT(lock->Holder != self);

    blocker_operation = lock->HolderOperation;
    filesystem_lock_copy_name(blocker_task, (blocker_operation != NULL) ? lock->HolderName : NULL);

    wait_start = filesystem_lock_stamp_now();
    while (xSemaphoreTake(lock->Handle, portMAX_DELAY) != pdTRUE)
    {
    }
    wait_us = filesystem_lock_elapsed_us(&wait_start);

    lock->Holder = self;
    lock->HolderOperation = operation;
    filesystem_lock_copy_name(lock->HolderName, task_name);
    lock->HoldStart = filesystem_lock_stamp_now();

    taskENTER_CRITICAL();
    filesystem_lock_sync_window(lock);
    filesystem_lock_update_wait(&lock->Window, wait_us, task_name, operation, blocker_task, blocker_operation);
    filesystem_lock_update_wait(&lock->Total, wait_us, task_name, operation, blocker_task, blocker_operation);
    taskEXIT_CRITICAL();
}

/**
 * @brief 记录本次持锁时长并放锁。
 * @param[in,out] lock 当前任务持有的锁。
 */
static void filesystem_lock_give(filesystem_lock_t *lock)
{
    const TaskHandle_t self = lock->Holder;
    const uint32_t hold_us = filesystem_lock_elapsed_us(&lock->HoldStart);
    uint32_t slot = FILESYSTEM_LOCK_LAST_HOLD_SLOTS;
    uint32_t index;

    taskENTER_CRITICAL();
    filesystem_lock_sync_window(lock);
    filesystem_lock_update_hold(&lock->Window, hold_us, lock->HolderName, lock->HolderOperation);
    filesystem_lock_update_hold(&lock->Total, hold_us, lock->HolderName, lock->HolderOperation);
    filesystem_lock_update_operation(lock->OpWindow, hold_us, lock->HolderOperation);
    filesystem_lock_update_operation(lock->OpTotal, hold_us, lock->HolderOperation);

    /* 只有卷锁记“最近一次公开调用的持锁时长”：表锁、槽表锁是调用中间的短锁，且可能在卷锁之后才放。 */
    if ((lock - filesystem_locks) < (ptrdiff_t)FILESYSTEM_LOCK_VOLUME_COUNT)
    {
        for (index = 0U; index < FILESYSTEM_LOCK_LAST_HOLD_SLOTS; index++)
        {
            if (filesystem_lock_last_holds[index].Task == self)
            {
                slot = index;
                break;
            }
        }
        for (index = 0U; (slot == FILESYSTEM_LOCK_LAST_HOLD_SLOTS) && (index < FILESYSTEM_LOCK_LAST_HOLD_SLOTS);
             index++)
        {
            if (filesystem_lock_last_holds[index].Task == NULL)
            {
                slot = index;
            }
        }
        if (slot == FILESYSTEM_LOCK_LAST_HOLD_SLOTS)
        {
            slot = 0U;
        }
        filesystem_lock_last_holds[slot].Task = self;
        filesystem_lock_last_holds[slot].HoldUs = hold_us;
    }
    taskEXIT_CRITICAL();

    lock->Holder = NULL;
    lock->HolderOperation = NULL;
    lock->HolderName[0] = '\0';
    (void)xSemaphoreGive(lock->Handle);
}

/**
 * @brief 取卷对应的那把锁；全局一把时恒为同一把。
 * @param[in] volume 已校验的卷。
 * @return 对应的锁。
 */
static filesystem_lock_t *filesystem_lock_for_volume(Service_Filesystem_VolumeTypeDef volume)
{
#if FILESYSTEM_LOCK_PER_VOLUME
    return &filesystem_locks[(volume == SERVICE_FILESYSTEM_VOLUME_FLASH) ? FILESYSTEM_LOCK_FLASH_INDEX : 0U];
#else
    (void)volume;
    return &filesystem_locks[0];
#endif
}

/**
 * @brief 拿卷锁（全局一把时拿那一把）。
 * @param[in] volume 已校验的卷。
 * @param[in] operation 公开操作名（静态字符串），进入统计。
 * @note  一次公开调用拿一次，返回前放，不跨调用持有；每卷一把时先拿卷锁再拿表锁。
 */
void filesystem_lock_acquire(Service_Filesystem_VolumeTypeDef volume, const char *operation)
{
    filesystem_lock_take(filesystem_lock_for_volume(volume), operation);
}

/**
 * @brief 放卷锁。
 * @param[in] volume 与 acquire 相同的卷。
 */
void filesystem_lock_release(Service_Filesystem_VolumeTypeDef volume)
{
    filesystem_lock_give(filesystem_lock_for_volume(volume));
}

#if FILESYSTEM_LOCK_PER_VOLUME
/**
 * @brief 拿打开表锁：包住会碰 FatFs `Files[]` 的调用和 Service 槽表操作。
 * @param[in] operation 公开操作名（静态字符串）。
 * @note  只能在已持有某把卷锁、或只做槽表短操作时拿；反向（持表锁再拿卷锁）不允许。
 */
void filesystem_lock_table_acquire(const char *operation)
{
    filesystem_lock_take(&filesystem_locks[FILESYSTEM_LOCK_TABLE_INDEX], operation);
}

/**
 * @brief 放打开表锁。
 */
void filesystem_lock_table_release(void)
{
    filesystem_lock_give(&filesystem_locks[FILESYSTEM_LOCK_TABLE_INDEX]);
}

/**
 * @brief 拿槽表锁：只保护 Service 的文件槽、目录槽表，持有期间只读写内存。
 * @param[in] operation 槽表操作名（静态字符串）。
 * @note  最内层的锁：持有它时不再拿任何别的锁，所以可以在卷锁或打开表锁之内拿。
 */
void filesystem_lock_slot_acquire(const char *operation)
{
    filesystem_lock_take(&filesystem_locks[FILESYSTEM_LOCK_SLOT_INDEX], operation);
}

/**
 * @brief 放槽表锁。
 */
void filesystem_lock_slot_release(void)
{
    filesystem_lock_give(&filesystem_locks[FILESYSTEM_LOCK_SLOT_INDEX]);
}
#endif

/**
 * @brief 当前固件是否为每卷一把锁。
 * @return true 为每卷一把加表锁；false 为全局一把。
 */
bool Service_Filesystem_SpikePerVolume(void)
{
    return FILESYSTEM_LOCK_PER_VOLUME != 0;
}

/**
 * @brief 打开表锁是否为窄范围（只包 f_open、f_opendir 和按定位符打开）。
 * @return true 为窄表锁；false 为宽表锁，或全局一把锁时没有表锁。
 */
bool Service_Filesystem_SpikeTableNarrow(void)
{
    return FILESYSTEM_LOCK_TABLE_NARROW != 0;
}

/**
 * @brief 观测用的锁个数。
 * @return 全局一把时为 1，每卷一把时为 4（SD、Flash、打开表锁、槽表锁）。
 */
uint32_t Service_Filesystem_SpikeLockCount(void)
{
    return FILESYSTEM_LOCK_COUNT;
}

/**
 * @brief 观测用的锁名。
 * @param[in] index 0 起始的锁序号。
 * @return 静态字符串；越界返回 "?"。
 */
const char *Service_Filesystem_SpikeLockName(uint32_t index)
{
#if FILESYSTEM_LOCK_PER_VOLUME
    static const char *const names[FILESYSTEM_LOCK_COUNT] = {"SD", "FLASH", "TABLE", "SLOT"};
#else
    static const char *const names[FILESYSTEM_LOCK_COUNT] = {"GLOBAL"};
#endif

    return (index < FILESYSTEM_LOCK_COUNT) ? names[index] : "?";
}

/**
 * @brief 拷出一把锁的窗口统计与自启动统计。
 * @param[in] index 0 起始的锁序号。
 * @param[out] window 阶段窗口统计，可为 NULL。
 * @param[out] total 自启动统计，可为 NULL。
 * @return true 序号有效；false 越界。
 */
bool Service_Filesystem_SpikeGetLockStats(uint32_t index,
                                          Service_Filesystem_SpikeLockStatsTypeDef *window,
                                          Service_Filesystem_SpikeLockStatsTypeDef *total)
{
    if (index >= FILESYSTEM_LOCK_COUNT)
    {
        return false;
    }

    taskENTER_CRITICAL();
    filesystem_lock_sync_window(&filesystem_locks[index]);
    if (window != NULL)
    {
        *window = filesystem_locks[index].Window;
    }
    if (total != NULL)
    {
        *total = filesystem_locks[index].Total;
    }
    taskEXIT_CRITICAL();
    return true;
}

/**
 * @brief 拷出一把锁按操作分开的最长持有，窗口与自启动各一份。
 * @param[in] index 0 起始的锁序号。
 * @param[out] window 阶段窗口的表，长度 SERVICE_FILESYSTEM_SPIKE_OP_MAX，可为 NULL。
 * @param[out] total 自启动的表，长度 SERVICE_FILESYSTEM_SPIKE_OP_MAX，可为 NULL。
 * @return 窗口表里已使用的条目数；序号越界返回 0。
 */
uint32_t Service_Filesystem_SpikeGetOpStats(uint32_t index,
                                            Service_Filesystem_SpikeOpStatsTypeDef *window,
                                            Service_Filesystem_SpikeOpStatsTypeDef *total)
{
    uint32_t used = 0U;
    uint32_t item;

    if (index >= FILESYSTEM_LOCK_COUNT)
    {
        return 0U;
    }

    taskENTER_CRITICAL();
    filesystem_lock_sync_window(&filesystem_locks[index]);
    if (window != NULL)
    {
        memcpy(window, filesystem_locks[index].OpWindow, sizeof(filesystem_locks[index].OpWindow));
    }
    if (total != NULL)
    {
        memcpy(total, filesystem_locks[index].OpTotal, sizeof(filesystem_locks[index].OpTotal));
    }
    for (item = 0U; item < SERVICE_FILESYSTEM_SPIKE_OP_MAX; item++)
    {
        if (filesystem_locks[index].OpWindow[item].Operation != NULL)
        {
            used++;
        }
    }
    taskEXIT_CRITICAL();
    return used;
}

/**
 * @brief 开始新的统计窗口：各锁的窗口统计在下次更新或读取时清零。
 */
void Service_Filesystem_SpikeWindowReset(void)
{
    filesystem_lock_window_epoch++;
}

/**
 * @brief 调用者任务最近一次完成的公开调用的持锁时长，读取后清零。
 * @return 微秒；该任务还没有记录、或上一次调用没有拿到锁（例如句柄无效）、或已被读过时为 0。
 *         每卷一把时是该次调用的卷锁持锁时长（表锁、槽表锁不记入）。
 * @note  按任务记录，其他任务随后拿锁不会覆盖它，所以调用者可在调用返回后立即读取。
 *        读取即清零，所以没拿到锁就返回的调用不会读到上一次调用的旧值。
 */
uint32_t Service_Filesystem_SpikeLastHoldUs(void)
{
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    uint32_t hold_us = 0U;
    uint32_t index;

    taskENTER_CRITICAL();
    for (index = 0U; index < FILESYSTEM_LOCK_LAST_HOLD_SLOTS; index++)
    {
        if (filesystem_lock_last_holds[index].Task == self)
        {
            hold_us = filesystem_lock_last_holds[index].HoldUs;
            filesystem_lock_last_holds[index].HoldUs = 0U;
            break;
        }
    }
    taskEXIT_CRITICAL();
    return hold_us;
}

/**
 * @brief GUI 或播放任务调用文件接口的累计次数。
 * @return 次数；正常应为 0。
 */
uint32_t Service_Filesystem_SpikeForbiddenCalls(void)
{
    return filesystem_lock_forbidden_calls;
}
