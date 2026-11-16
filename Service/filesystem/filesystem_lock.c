/**
 ******************************************************************************
 * @file    filesystem_lock.c
 * @brief   Filesystem 的卷锁、窄表锁、调用者拒绝名单与持锁统计。
 *
 * @details
 *          三把互斥锁（FreeRTOS 静态互斥量，带优先级继承，不占堆）：SD 卷锁、Flash 卷锁、表锁。
 *          锁在第一次用到时创建（临界区内，幂等），所以不要求哪个初始化函数先于哪个调用者。
 *          每把锁记着当前持有者，用来断言：同一任务重复拿同一把锁（`configUSE_RECURSIVE_MUTEXES`
 *          为 0，真拿就是死锁）、同时持两把卷锁、持表锁时再拿卷锁、不持卷锁却拿表锁。
 *
 *          持锁统计（`FILESYSTEM_LOCK_STATS_ENABLE`，Debug 开、Release 关）：每把锁只记开机以来的最长持有、
 *          最长等待、各自对应的操作和拿取次数；公开调用放掉卷锁之后，发现距上次汇总已满节拍，就投一批汇总日志
 *          （每把锁一行）。日志只在锁放掉之后投，所以不会在持锁时被调用。
 ******************************************************************************
 */

#include "Service/filesystem/filesystem_lock.h"

#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_service.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#if FILESYSTEM_LOCK_STATS_ENABLE
#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/log/log_service.h"
#endif

#define FILESYSTEM_LOCK_TABLE_INDEX 2U  /* 表锁的下标，紧跟在两把卷锁之后。 */
#define FILESYSTEM_LOCK_COUNT 3U        /* 锁的总数；没有槽表锁，也没有第三把卷级锁。 */

#if FILESYSTEM_LOCK_STATS_ENABLE
/* 周期计数器是 32 位的，480 MHz 下约 8.9 s 回绕；区间超过这个 tick 数（4 s，留足余量）就改用 tick 量。 */
#define FILESYSTEM_LOCK_STATS_DWT_LIMIT_TICKS 4000U

_Static_assert(configTICK_RATE_HZ <= 1000000U, "tick fallback converts ticks to microseconds");

/** @brief 一个时间点：周期计数 + tick，用来在周期计数回绕前后都量对。 */
typedef struct
{
    uint32_t cycles;
    TickType_t tick;
} filesystem_lock_stamp_t;

/** @brief 一把锁开机以来的统计。 */
typedef struct
{
    uint32_t acquisitions;
    uint32_t hold_max_us;
    filesystem_lock_op_t hold_operation;
    uint32_t wait_max_us;
    filesystem_lock_op_t wait_operation;
} filesystem_lock_stats_t;
#endif

/** @brief 一把锁：互斥量、当前持有者（及其操作、起点）、统计。持有者等字段只在持锁期间由持锁任务写。 */
typedef struct
{
    StaticSemaphore_t storage;
    SemaphoreHandle_t handle;
    volatile TaskHandle_t holder;
#if FILESYSTEM_LOCK_STATS_ENABLE
    filesystem_lock_op_t holder_operation;
    filesystem_lock_stamp_t hold_start;
    filesystem_lock_stats_t stats;
#endif
} filesystem_lock_entry_t;

static filesystem_lock_entry_t filesystem_locks[FILESYSTEM_LOCK_COUNT];
static volatile bool filesystem_locks_created;

/* 调用者拒绝名单：APP 的 Create Task 在创建完 GUI 与播放任务之后立即登记它们（只有 Create Task 写；被登记的任务那时可能已经在运行，
 * 它们按设计不调用文件接口），之后各任务在公开入口只读。
 * 先写句柄、后加计数，读者按计数读，所以单核上不会读到半个条目。 */
static TaskHandle_t filesystem_denied_tasks[FILESYSTEM_DENIED_CALLER_MAX];
static volatile uint32_t filesystem_denied_count;

#if FILESYSTEM_LOCK_STATS_ENABLE
/* 汇总日志用的静态缓冲：只有“claim 到本次汇总”的那一个任务用，所以不放在调用者的栈上。 */
static char filesystem_lock_report_line[FILESYSTEM_LOCK_STATS_LINE_BYTES];
static filesystem_lock_stats_t filesystem_lock_report_snapshot[FILESYSTEM_LOCK_COUNT];
static volatile bool filesystem_lock_report_busy;
static TickType_t filesystem_lock_report_last_tick;

/* 操作的短名，须与 filesystem_lock_op_t 同序；每项最多 9 个字符（超长会在编译时报“初始化字符串太长”），
 * 这样一行汇总日志最长约 80 个字符，放得进 Service Log 的 127 字符。 */
static const char filesystem_lock_operation_names[FILESYSTEM_OP_COUNT][10] = {
    [FILESYSTEM_OP_NONE] = "-",
    [FILESYSTEM_OP_OPEN] = "Open",
    [FILESYSTEM_OP_OPEN_LOCATOR] = "OpenLoc",
    [FILESYSTEM_OP_OPEN_DIR] = "OpenDir",
    [FILESYSTEM_OP_READ] = "Read",
    [FILESYSTEM_OP_WRITE] = "Write",
    [FILESYSTEM_OP_SEEK] = "Seek",
    [FILESYSTEM_OP_TELL] = "Tell",
    [FILESYSTEM_OP_SIZE] = "Size",
    [FILESYSTEM_OP_SYNC] = "Sync",
    [FILESYSTEM_OP_CLOSE] = "Close",
    [FILESYSTEM_OP_REMOVE] = "Remove",
    [FILESYSTEM_OP_RENAME] = "Rename",
    [FILESYSTEM_OP_INFO] = "Info",
    [FILESYSTEM_OP_READ_DIR] = "ReadDir",
    [FILESYSTEM_OP_REWIND] = "Rewind",
    [FILESYSTEM_OP_CLOSE_DIR] = "CloseDir",
    [FILESYSTEM_OP_MKDIR] = "MkDir",
    [FILESYSTEM_OP_RMDIR] = "RmDir",
    [FILESYSTEM_OP_INIT] = "Init",
    [FILESYSTEM_OP_MOUNT] = "Mount",
    [FILESYSTEM_OP_UNMOUNT] = "Unmount",
    [FILESYSTEM_OP_FORMAT] = "Format",
    [FILESYSTEM_OP_RECOVER] = "Recover",
    [FILESYSTEM_OP_RECLAIM] = "Reclaim",
    [FILESYSTEM_OP_FLASH_DIAG] = "FlashDiag",
    [FILESYSTEM_OP_START] = "Start",
    [FILESYSTEM_OP_REFRESH] = "Refresh",
};

static const char *const filesystem_lock_names[FILESYSTEM_LOCK_COUNT] = {"sd", "flash", "table"};
#endif

/**
 * @brief 判断卷枚举是否可作为锁的下标。
 * @param[in] volume 调用者传入的卷。
 * @return true 表示 SD 或 Flash。
 */
bool filesystem_lock_volume_valid(Service_Filesystem_VolumeTypeDef volume)
{
    return (volume == SERVICE_FILESYSTEM_VOLUME_SD) || (volume == SERVICE_FILESYSTEM_VOLUME_FLASH);
}

/**
 * @brief 登记一个不得调用文件接口的任务（GUI 与播放任务）。
 * @param[in] task 任务句柄，非空。重复登记同一个句柄无效果。
 * @note  APP 的 Create Task 在创建完这两个任务之后立即调用（`app_tasks.c`）。被登记的任务那时可能已经在运行
 *        （播放任务的优先级高于 Create Task，创建后立刻跑到第一次阻塞），但它们按设计不调用文件接口，
 *        所以创建到登记之间不会有调用。登记之后，这个任务只要进入任何 Filesystem 公开函数（文件、目录、卷生命周期、
 *        Flash 诊断；`Service_Filesystem_GbkToUnicode` 除外），入口立即 `configASSERT` 停机，Release 也保留，
 *        检查在等锁之前。登记超过 `FILESYSTEM_DENIED_CALLER_MAX` 个、登记空句柄同样断言停机。
 *        用禁用名单而不是允许名单，是为了新增任务时共用层不用改。
 */
void Service_Filesystem_DenyCaller(TaskHandle_t task)
{
    uint32_t count = filesystem_denied_count;

    configASSERT(task != NULL);
    for (uint32_t index = 0U; index < count; index++)
    {
        if (filesystem_denied_tasks[index] == task)
        {
            return;
        }
    }

    configASSERT(count < FILESYSTEM_DENIED_CALLER_MAX);
    filesystem_denied_tasks[count] = task;
    filesystem_denied_count = count + 1U;
}

/**
 * @brief 检查调用者不在拒绝名单里。
 * @note  每个会等锁的公开入口的第一件事，在等锁之前：名单里的任务（GUI、播放）进来就 `configASSERT` 停机，
 *        Release 也保留（`configASSERT` 不受 `NDEBUG` 影响）。名单只有几项，一次检查只是几次比较。
 */
void filesystem_lock_check_caller(void)
{
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    const uint32_t count = filesystem_denied_count;

    for (uint32_t index = 0U; index < count; index++)
    {
        configASSERT(filesystem_denied_tasks[index] != self);
    }
}

/**
 * @brief 断言调用者此刻不持有任何一把 Filesystem 锁（两把卷锁和表锁）。
 * @note  闸的等待在拿锁之前：持着锁去等闸，卷任务要这把锁才能完成对账并开闸，就是死锁；
 *        所以进门等闸之前先检查，闸开着时也检查，错误因此每次都暴露而不是只在关闸时。
 *        停机用 `configASSERT`，Release 也保留。读持有者字段不需要持锁：只有持锁的任务会把自己写进去，
 *        所以“持有者是我”只可能是真的。
 */
void filesystem_lock_assert_caller_holds_none(void)
{
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();

    for (uint32_t index = 0U; index < FILESYSTEM_LOCK_COUNT; index++)
    {
        configASSERT((filesystem_locks[index].holder == NULL) || (filesystem_locks[index].holder != self));
    }
}

#if FILESYSTEM_LOCK_STATS_ENABLE
/**
 * @brief 取当前时间点。
 * @return 周期计数与 tick。
 */
static filesystem_lock_stamp_t filesystem_lock_stamp_now(void)
{
    filesystem_lock_stamp_t stamp;

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
static uint32_t filesystem_lock_stamp_diff_us(const filesystem_lock_stamp_t *from, const filesystem_lock_stamp_t *to)
{
    const TickType_t tick_delta = to->tick - from->tick;
    const uint32_t cycles_per_us = CortexM7CycleCounter_GetFrequencyHz() / 1000000U;
    uint64_t microseconds;

    if ((cycles_per_us == 0U) || (tick_delta >= FILESYSTEM_LOCK_STATS_DWT_LIMIT_TICKS))
    {
        microseconds = (uint64_t)tick_delta * (1000000U / (uint32_t)configTICK_RATE_HZ);
        return (microseconds > UINT32_MAX) ? UINT32_MAX : (uint32_t)microseconds;
    }

    return (to->cycles - from->cycles) / cycles_per_us;
}

/**
 * @brief 刚拿到锁：记下持有起点，更新拿取次数与最长等待。
 * @param[in,out] entry 刚拿到的锁，调用者已是它的持有者。
 * @param[in] operation 本次调用的操作。
 * @param[in] wait_start 开始等锁的时间点。
 */
static void filesystem_lock_stats_acquired(filesystem_lock_entry_t *entry,
                                           filesystem_lock_op_t operation,
                                           const filesystem_lock_stamp_t *wait_start)
{
    const filesystem_lock_stamp_t acquired = filesystem_lock_stamp_now();
    const uint32_t wait_us = filesystem_lock_stamp_diff_us(wait_start, &acquired);

    entry->hold_start = acquired;
    entry->holder_operation = operation;

    /* 汇总日志的读者在别的任务里，所以更新放在临界区内（几个字的赋值）。 */
    taskENTER_CRITICAL();
    entry->stats.acquisitions++;
    if (wait_us > entry->stats.wait_max_us)
    {
        entry->stats.wait_max_us = wait_us;
        entry->stats.wait_operation = operation;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief 即将放锁：用持有起点更新最长持有。
 * @param[in,out] entry 要放的锁，调用者仍是它的持有者。
 */
static void filesystem_lock_stats_releasing(filesystem_lock_entry_t *entry)
{
    const filesystem_lock_stamp_t released = filesystem_lock_stamp_now();
    const uint32_t hold_us = filesystem_lock_stamp_diff_us(&entry->hold_start, &released);

    taskENTER_CRITICAL();
    if (hold_us > entry->stats.hold_max_us)
    {
        entry->stats.hold_max_us = hold_us;
        entry->stats.hold_operation = entry->holder_operation;
    }
    taskEXIT_CRITICAL();
    entry->holder_operation = FILESYSTEM_OP_NONE;
}

/** @brief 一行汇总日志的拼接状态。 */
typedef struct
{
    char *buffer;
    uint32_t capacity;
    uint32_t length;
} filesystem_lock_line_t;

/**
 * @brief 往行尾追加文本，装不下就截断。
 * @param[in,out] line 拼接状态。
 * @param[in] text 以 '\0' 结尾的文本。
 */
static void filesystem_lock_line_text(filesystem_lock_line_t *line, const char *text)
{
    while ((*text != '\0') && ((line->length + 1U) < line->capacity))
    {
        line->buffer[line->length++] = *text++;
    }
    line->buffer[line->length] = '\0';
}

/**
 * @brief 往行尾追加一个十进制无符号数。不用 snprintf：它在调用者的栈上要几百字节，而汇总可能发生在任何调用文件接口的任务里。
 * @param[in,out] line 拼接状态。
 * @param[in] value 数值。
 */
static void filesystem_lock_line_u32(filesystem_lock_line_t *line, uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;

    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    while ((count != 0U) && ((line->length + 1U) < line->capacity))
    {
        line->buffer[line->length++] = digits[--count];
    }
    line->buffer[line->length] = '\0';
}

/**
 * @brief 操作的短名。
 * @param[in] operation 操作。
 * @return 短名；表里漏填的返回 "?"。
 */
static const char *filesystem_lock_operation_name(filesystem_lock_op_t operation)
{
    const char *name = ((unsigned int)operation < (unsigned int)FILESYSTEM_OP_COUNT)
                           ? filesystem_lock_operation_names[operation]
                           : "?";

    return (name[0] != '\0') ? name : "?";
}

/**
 * @brief 把一把锁的统计拼成一行：`<锁> n=<次数> hold=<微秒>us:<操作> wait=<微秒>us:<操作>`。
 * @param[in] index 锁的下标。
 * @param[in] stats 该锁的统计快照。
 * @return 静态缓冲里的行。
 */
static const char *filesystem_lock_format_line(uint32_t index, const filesystem_lock_stats_t *stats)
{
    filesystem_lock_line_t line = {filesystem_lock_report_line, FILESYSTEM_LOCK_STATS_LINE_BYTES, 0U};

    filesystem_lock_line_text(&line, filesystem_lock_names[index]);
    filesystem_lock_line_text(&line, " n=");
    filesystem_lock_line_u32(&line, stats->acquisitions);
    filesystem_lock_line_text(&line, " hold=");
    filesystem_lock_line_u32(&line, stats->hold_max_us);
    filesystem_lock_line_text(&line, "us:");
    filesystem_lock_line_text(&line, filesystem_lock_operation_name(stats->hold_operation));
    filesystem_lock_line_text(&line, " wait=");
    filesystem_lock_line_u32(&line, stats->wait_max_us);
    filesystem_lock_line_text(&line, "us:");
    filesystem_lock_line_text(&line, filesystem_lock_operation_name(stats->wait_operation));
    return filesystem_lock_report_line;
}

/**
 * @brief 节拍到了就投一批汇总日志（每把锁一行）。
 * @note  只在放掉卷锁之后调用，所以投递日志时调用者不持任何文件锁（`Service_Log_Post` 本来也不阻塞）。
 *        同一时刻只有一个任务能 claim 到本批；没有文件接口的调用就没有汇总，不会有空转的节拍。
 */
static void filesystem_lock_stats_report_if_due(void)
{
    const TickType_t now = xTaskGetTickCount();
    bool claimed = false;

    if ((TickType_t)(now - filesystem_lock_report_last_tick) < pdMS_TO_TICKS(FILESYSTEM_LOCK_STATS_PERIOD_MS))
    {
        return;
    }

    taskENTER_CRITICAL();
    if (!filesystem_lock_report_busy &&
        ((TickType_t)(now - filesystem_lock_report_last_tick) >= pdMS_TO_TICKS(FILESYSTEM_LOCK_STATS_PERIOD_MS)))
    {
        filesystem_lock_report_busy = true;
        filesystem_lock_report_last_tick = now;
        for (uint32_t index = 0U; index < FILESYSTEM_LOCK_COUNT; index++)
        {
            filesystem_lock_report_snapshot[index] = filesystem_locks[index].stats;
        }
        claimed = true;
    }
    taskEXIT_CRITICAL();

    if (!claimed)
    {
        return;
    }

    for (uint32_t index = 0U; index < FILESYSTEM_LOCK_COUNT; index++)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO,
                               "FSLOCK",
                               filesystem_lock_format_line(index, &filesystem_lock_report_snapshot[index]));
    }
    filesystem_lock_report_busy = false;
}
#endif /* FILESYSTEM_LOCK_STATS_ENABLE */

/**
 * @brief 幂等地创建三把静态互斥量。
 * @note  第一次调用在临界区内创建，之后只读一个标志；调度器启动前后都可以调用。
 */
static void filesystem_lock_ensure_created(void)
{
    if (filesystem_locks_created)
    {
        return;
    }

    taskENTER_CRITICAL();
    if (!filesystem_locks_created)
    {
        for (uint32_t index = 0U; index < FILESYSTEM_LOCK_COUNT; index++)
        {
            filesystem_locks[index].handle = xSemaphoreCreateMutexStatic(&filesystem_locks[index].storage);
            configASSERT(filesystem_locks[index].handle != NULL);
        }
#if FILESYSTEM_LOCK_STATS_ENABLE
        filesystem_lock_report_last_tick = xTaskGetTickCount();
#endif
        filesystem_locks_created = true;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief 等到拿到这把锁，并记下持有者；统计开关打开时同时记录等待与持有起点。
 * @param[in,out] entry 要拿的锁。
 * @param[in] operation 本次公开调用的操作。
 * @param[in] self 当前任务。
 * @note  `INCLUDE_vTaskSuspend` 为 0，`portMAX_DELAY` 只是约 49.7 天的超时，所以超时后继续等：
 *        调用者要的是“等到拿到为止”，不能在没有锁的情况下往下走。
 */
static void filesystem_lock_enter(filesystem_lock_entry_t *entry, filesystem_lock_op_t operation, TaskHandle_t self)
{
#if FILESYSTEM_LOCK_STATS_ENABLE
    const filesystem_lock_stamp_t wait_start = filesystem_lock_stamp_now();
#endif

    while (xSemaphoreTake(entry->handle, portMAX_DELAY) != pdTRUE)
    {
    }
    entry->holder = self;
#if FILESYSTEM_LOCK_STATS_ENABLE
    filesystem_lock_stats_acquired(entry, operation, &wait_start);
#else
    (void)operation;
#endif
}

/**
 * @brief 清掉持有者并放锁；放锁前断言调用者就是持有者。
 * @param[in,out] entry 要放的锁。
 */
static void filesystem_lock_leave(filesystem_lock_entry_t *entry)
{
    BaseType_t given;

    configASSERT(entry->holder == xTaskGetCurrentTaskHandle());
#if FILESYSTEM_LOCK_STATS_ENABLE
    filesystem_lock_stats_releasing(entry);
#endif
    entry->holder = NULL;
    given = xSemaphoreGive(entry->handle);
    configASSERT(given == pdTRUE);
    (void)given;
}

/**
 * @brief 拿所属卷的卷锁，等到拿到为止。
 * @param[in] volume 目标卷，必须合法（调用前用 `filesystem_lock_volume_valid` 挡掉）。
 * @param[in] operation 本次公开调用的操作，只用于持锁统计。
 * @note  第一次调用时创建三把锁（静态存储，不占堆）。同一任务已持这把锁、已持另一卷的锁、
 *        或正持表锁时断言停机。
 */
void filesystem_lock_take(Service_Filesystem_VolumeTypeDef volume, filesystem_lock_op_t operation)
{
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    filesystem_lock_entry_t *entry;
    const filesystem_lock_entry_t *other;

    configASSERT(filesystem_lock_volume_valid(volume));
    filesystem_lock_ensure_created();
    entry = &filesystem_locks[volume];
    other = &filesystem_locks[(volume == SERVICE_FILESYSTEM_VOLUME_SD) ? SERVICE_FILESYSTEM_VOLUME_FLASH
                                                                         : SERVICE_FILESYSTEM_VOLUME_SD];

    /* 公开函数之间不互相调用，所以不会出现重复拿同一把锁、同时持两把卷锁、持表锁时再拿卷锁；
     * 出现了就是调用关系写错了，立刻停机比死锁好找。持有者为空表示锁空闲。 */
    configASSERT((entry->holder == NULL) || (entry->holder != self));
    configASSERT((other->holder == NULL) || (other->holder != self));
    configASSERT((filesystem_locks[FILESYSTEM_LOCK_TABLE_INDEX].holder == NULL) ||
                 (filesystem_locks[FILESYSTEM_LOCK_TABLE_INDEX].holder != self));

    filesystem_lock_enter(entry, operation, self);
}

/**
 * @brief 放所属卷的卷锁；之后在统计开关打开时按节拍投一批汇总日志（锁已放，不在持锁时投递）。
 * @param[in] volume 目标卷。
 * @note  必须由拿锁的任务调用，否则断言停机。
 */
void filesystem_lock_give(Service_Filesystem_VolumeTypeDef volume)
{
    configASSERT(filesystem_lock_volume_valid(volume));
    filesystem_lock_leave(&filesystem_locks[volume]);
#if FILESYSTEM_LOCK_STATS_ENABLE
    filesystem_lock_stats_report_if_due();
#endif
}

/**
 * @brief 拿窄表锁，等到拿到为止；只能在已持一把卷锁时调用。
 * @param[in] operation 表锁内的打开操作：`FILESYSTEM_OP_OPEN`、`_OPEN_LOCATOR` 或 `_OPEN_DIR`。
 * @note  已持表锁（重复拿）或没有持卷锁（锁序反了）时断言停机。表锁内不得拿任何别的锁。
 */
void filesystem_lock_table_take(filesystem_lock_op_t operation)
{
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    filesystem_lock_entry_t *table = &filesystem_locks[FILESYSTEM_LOCK_TABLE_INDEX];

    configASSERT(filesystem_locks_created);
    /* 表锁只在卷锁内拿（卷锁 → 表锁），且不重复拿。 */
    configASSERT((filesystem_locks[SERVICE_FILESYSTEM_VOLUME_SD].holder == self) ||
                 (filesystem_locks[SERVICE_FILESYSTEM_VOLUME_FLASH].holder == self));
    configASSERT((table->holder == NULL) || (table->holder != self));

    filesystem_lock_enter(table, operation, self);
}

/**
 * @brief 放窄表锁。表锁在卷锁内，所以这里不投汇总日志，等放卷锁之后再投。
 * @note  必须由拿锁的任务调用，否则断言停机。
 */
void filesystem_lock_table_give(void)
{
    filesystem_lock_leave(&filesystem_locks[FILESYSTEM_LOCK_TABLE_INDEX]);
}
