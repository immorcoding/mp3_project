/**
 ******************************************************************************
 * @file    foreground_loop.c
 * @brief   前台任务一圈的判断实现（见 foreground_loop.h）。
 *
 * @details
 *          只含“调一遍 step、量耗时、算睡眠时长、等通知、叫醒”，没有 `for(;;)` 和任务创建，所以能在主机上用共用 FreeRTOS 桩测。
 *          通知用 `eSetBits`：连叫几次合并成一次醒；step 运行期间到达的通知留在槽里，等待立刻返回，不会丢。
 *          耗时用 DWT 周期计数器（`CortexM7CycleCounter_Read()`，32 位无符号差，回绕不影响）换算成微秒；计数器没启动（频率 0）时
 *          耗时记 0，不会误报。最长值只由前台任务写，不跨任务读，所以不需要锁。
 ******************************************************************************
 */

#include "APP/tasks/foreground/foreground_loop.h"

#include <stdio.h>
#include <string.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/log/log_service.h"

static const char foreground_log_tag[] = "FOREGROUND";

/* 前台任务的句柄：任务入口在第一圈之前写一次，之后只读；32 位指针读写原子。绑定之前 wake 是空操作。 */
static volatile TaskHandle_t foreground_loop_task;

/* 每个应用 step 的最长耗时（微秒）与应用名（注册表里的静态字符串）；只由前台任务写。 */
static uint32_t foreground_loop_max_us[FOREGROUND_STEP_APP_MAX];
static const char *foreground_loop_names[FOREGROUND_STEP_APP_MAX];

/* 最长值有刷新、还没汇总进 INFO 行。 */
static bool foreground_loop_max_dirty;

/* 记 INFO 汇总的最近时刻（毫秒，从 0 起算：开机后第一个周期满了才记第一行）。 */
static uint32_t foreground_loop_report_last_ms;

/* 汇总行的缓冲，放静态区，不占前台任务栈（step 里不放大局部变量）。 */
static char foreground_loop_line[FOREGROUND_LOG_LINE_BYTES];

/**
 * @brief  把周期数换成微秒。
 * @param[in] cycles 周期数（32 位无符号差）。
 * @return 微秒；计数器没启动（频率不足 1 MHz）时为 0。
 */
static uint32_t foreground_loop_cycles_to_us(uint32_t cycles)
{
    const uint32_t cycles_per_us = CortexM7CycleCounter_GetFrequencyHz() / 1000000U;

    return (cycles_per_us == 0U) ? 0U : (cycles / cycles_per_us);
}

/**
 * @brief  一个 step 跑完之后记录它的耗时：刷新最长值，超过阈值且刷新了才记 WARN。
 * @param[in] app_index 应用在表里的序号。
 * @param[in] name 应用名，用于日志；NULL 当空串。
 * @param[in] elapsed_us 这一次 step 的耗时。
 * @note   只在刷新最长值时记，所以一个持续偏慢的应用不会刷屏；记了也不停机。WARN 行放不下时被截断，不会溢出缓冲。
 */
static void foreground_loop_note_step(size_t app_index, const char *name, uint32_t elapsed_us)
{
    if ((app_index >= (size_t)FOREGROUND_STEP_APP_MAX) || (elapsed_us <= foreground_loop_max_us[app_index]))
    {
        return;
    }

    foreground_loop_max_us[app_index] = elapsed_us;
    foreground_loop_names[app_index] = name;
    if (elapsed_us > FOREGROUND_STEP_WARN_US)
    {
        (void)snprintf(foreground_loop_line, sizeof(foreground_loop_line),
                       "Foreground step slow app=%s us=%lu limit=%lu",
                       (name == NULL) ? "" : name,
                       (unsigned long)elapsed_us,
                       (unsigned long)FOREGROUND_STEP_WARN_US);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, foreground_log_tag, foreground_loop_line);
        return;
    }

    foreground_loop_max_dirty = true;
}

/**
 * @brief  把表里的每个应用的 `foreground_step` 各调一次，返回最小的等待毫秒数。
 * @param[in] rows 注册表行，空 step 跳过；`count` 为 0 时可以是 NULL。
 * @param[in] count 行数。
 * @return 最小返回值；没有任何 step 被调用时为 `FOREGROUND_WAIT_FOREVER`。
 * @note   不因为某个 step 返回 0 就跳过后面的应用：每圈每个应用恰好一次，顺序就是表的顺序。
 */
uint32_t foreground_loop_run_steps(const AppTable_EntryTypeDef *rows, size_t count)
{
    uint32_t smallest = FOREGROUND_WAIT_FOREVER;

    for (size_t index = 0U; (rows != NULL) && (index < count); index++)
    {
        uint32_t start_cycles;
        uint32_t wait_ms;

        if (rows[index].foreground_step == NULL)
        {
            continue;
        }

        start_cycles = CortexM7CycleCounter_Read();
        wait_ms = rows[index].foreground_step();
        foreground_loop_note_step(index,
                                  rows[index].name,
                                  foreground_loop_cycles_to_us(CortexM7CycleCounter_Read() - start_cycles));

        if (wait_ms < smallest)
        {
            smallest = wait_ms;
        }
    }

    return smallest;
}

/**
 * @brief  毫秒换成等待 tick。
 * @param[in] wait_ms 毫秒；0 马上，`FOREGROUND_WAIT_FOREVER` 一直等通知。
 * @return tick 数。非零毫秒向上取整，不会变成 0；有限的等待不会等于 `portMAX_DELAY`。
 * @note   `INCLUDE_vTaskSuspend` 为 0，`portMAX_DELAY` 在内核里只是约 49.7 天的有限超时，到期后任务多跑一圈，无害。
 */
TickType_t foreground_loop_wait_ticks(uint32_t wait_ms)
{
    uint64_t ticks;

    if (wait_ms == FOREGROUND_WAIT_FOREVER)
    {
        return portMAX_DELAY;
    }

    ticks = (((uint64_t)wait_ms * (uint64_t)configTICK_RATE_HZ) + 999ULL) / 1000ULL;
    if (ticks >= (uint64_t)portMAX_DELAY)
    {
        return (TickType_t)(portMAX_DELAY - 1U);
    }

    return (TickType_t)ticks;
}

/**
 * @brief  一圈：调一遍 step，调一次 `after_round`，再按最小返回值等通知。
 * @param[in] rows 注册表行。
 * @param[in] count 行数。
 * @param[in] after_round 每圈一次的回调，先于等待；NULL 跳过。不得阻塞。
 * @retval true 被通知叫醒（含 step 运行期间已到的通知）。
 * @retval false 等满了超时。
 */
bool foreground_loop_step(const AppTable_EntryTypeDef *rows, size_t count, void (*after_round)(void))
{
    const TickType_t wait_ticks = foreground_loop_wait_ticks(foreground_loop_run_steps(rows, count));
    uint32_t flags = 0U;

    if (after_round != NULL)
    {
        after_round();
    }

    return xTaskNotifyWaitIndexed((UBaseType_t)FOREGROUND_NOTIFY_EVENT,
                                  0U,
                                  FOREGROUND_NOTIFY_FLAG_EVENT_MASK,
                                  &flags,
                                  wait_ticks) == pdTRUE;
}

/**
 * @brief  第 app_index 个应用从启动起 step 的最长耗时。
 * @param[in] app_index 应用在表里的序号。
 * @return 微秒；序号越界或没有样本为 0。
 */
uint32_t foreground_loop_step_max_us(size_t app_index)
{
    return (app_index < (size_t)FOREGROUND_STEP_APP_MAX) ? foreground_loop_max_us[app_index] : 0U;
}

/**
 * @brief  把还没记出的最长值汇总成一行 INFO（`Foreground step max <应用名>=<微秒>us ...`）。
 * @param[in] now_ms 当前毫秒时刻（FreeRTOS tick，无符号差处理回绕）。
 * @note   至多每 `FOREGROUND_MAX_LOG_PERIOD_MS` 一行，没有新刷新时不记，所以稳态下几乎不占 Service Log 消息池（DIAG-3）。
 *         只列有样本的应用，名字取自记录最长值时的注册表行。行放不下时末字符改为 `~`，不溢出缓冲。
 *         超过阈值的最长值已经在 step 里记过 WARN，不进这一行的“新刷新”判断，但它的当前最长值仍会随下一行一起列出。
 */
void foreground_loop_report(uint32_t now_ms)
{
    size_t length;
    int written;

    if (!foreground_loop_max_dirty ||
        ((uint32_t)(now_ms - foreground_loop_report_last_ms) < (uint32_t)FOREGROUND_MAX_LOG_PERIOD_MS))
    {
        return;
    }

    length = (size_t)snprintf(foreground_loop_line, sizeof(foreground_loop_line), "Foreground step max");
    for (size_t index = 0U; index < (size_t)FOREGROUND_STEP_APP_MAX; index++)
    {
        if (foreground_loop_max_us[index] == 0U)
        {
            continue;
        }

        written = snprintf(&foreground_loop_line[length],
                           sizeof(foreground_loop_line) - length,
                           " %s=%luus",
                           (foreground_loop_names[index] == NULL) ? "" : foreground_loop_names[index],
                           (unsigned long)foreground_loop_max_us[index]);
        if ((written < 0) || ((size_t)written >= (sizeof(foreground_loop_line) - length)))
        {
            foreground_loop_line[sizeof(foreground_loop_line) - 2U] = '~'; /* 放不下：截断可见而不静默。 */
            foreground_loop_line[sizeof(foreground_loop_line) - 1U] = '\0';
            break;
        }
        length += (size_t)written;
    }

    foreground_loop_max_dirty = false;
    foreground_loop_report_last_ms = now_ms;
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, foreground_log_tag, foreground_loop_line);
}

/**
 * @brief  记下前台任务的句柄。
 * @param[in] task 前台任务句柄；传 NULL 取消绑定。
 */
void foreground_loop_bind(TaskHandle_t task)
{
    foreground_loop_task = task;
}

/**
 * @brief  清空最长值记录与汇总节拍。
 */
void foreground_loop_reset(void)
{
    (void)memset(foreground_loop_max_us, 0, sizeof(foreground_loop_max_us));
    (void)memset(foreground_loop_names, 0, sizeof(foreground_loop_names));
    foreground_loop_max_dirty = false;
    foreground_loop_report_last_ms = 0U;
}

/**
 * @brief  叫醒前台任务，让它马上把所有应用的 step 各调一遍。
 * @note   任务上下文调用，不得从 ISR 调用。任务入口还没绑定句柄时什么都不做：任务第一圈本来就调所有应用。
 */
void foreground_task_wake(void)
{
    const TaskHandle_t task = foreground_loop_task;

    if (task != NULL)
    {
        (void)xTaskNotifyIndexed(task,
                                 (UBaseType_t)FOREGROUND_NOTIFY_EVENT,
                                 FOREGROUND_NOTIFY_FLAG_WAKE,
                                 eSetBits);
    }
}
