/**
 * @file music_display_stats.c
 * @brief 显示延迟汇总：曲名、新窗口、拔卡后 Queue 清空三类延迟的样本计数与汇总行（DIAG-6、R5.1、R5.2）。
 *
 * @details
 *          Debug 开、Release 关（`MUSIC_DISPLAY_STATS_ENABLE`）。并发：样本由 GUI 任务写（`note_title`、`note_window`、`note_clear`），
 *          清空延迟的起点由前台任务写（`set_clear_origin`）并由 GUI 消费，汇总由前台任务读并清零本节拍的计数——节拍计数
 *          有两个任务读写同一个字，读-改-写之间被抢占会丢一两个样本，只影响一行诊断，不影响数据；所以没有锁，也不进 `ui_cell`。
 *          日志只在前台任务里投，不在 GUI 帧路径上投（DIAG-3）。
 */

#include "APP/apps/music/music_display_stats.h"

#if (MUSIC_DISPLAY_STATS_ENABLE != 0U)

#include <stdbool.h>
#include <stdio.h>

#include "Service/log/log_service.h"

static const char music_display_stats_tag[] = "DISPLAY";

/** @brief 一类延迟的累计：本节拍的样本数与最大值，以及开机以来的最大值。 */
typedef struct
{
    uint32_t Count;   /**< 本节拍的样本数。 */
    uint32_t Max;     /**< 本节拍的最大延迟（毫秒）。 */
    uint32_t Top;     /**< 开机以来的最大延迟（毫秒），不清零。 */
} MusicDisplayStats_KindTypeDef;

static MusicDisplayStats_KindTypeDef music_display_stats_title;
static MusicDisplayStats_KindTypeDef music_display_stats_window;
static MusicDisplayStats_KindTypeDef music_display_stats_clear;

static volatile bool music_display_stats_origin_valid; /* 前台记下了卡检测边沿时刻，GUI 还没消费。 */
static volatile uint32_t music_display_stats_origin_ms;
static uint32_t music_display_stats_last_report_ms;    /* 上一行汇总的毫秒时刻，从 0 起算：开机后第一个节拍满了才投第一行。 */

/**
 * @brief 记一个样本。
 * @param[in,out] kind 这一类的累计。
 * @param[in] latency_ms 延迟（毫秒）。
 */
static void music_display_stats_add(MusicDisplayStats_KindTypeDef *kind, uint32_t latency_ms)
{
    kind->Count++;
    if (latency_ms > kind->Max)
    {
        kind->Max = latency_ms;
    }
    if (latency_ms > kind->Top)
    {
        kind->Top = latency_ms;
    }
}

/**
 * @brief GUI 一侧：记一次曲名延迟。
 * @param[in] latency_ms 毫秒。
 */
void music_display_stats_note_title(uint32_t latency_ms)
{
    music_display_stats_add(&music_display_stats_title, latency_ms);
}

/**
 * @brief GUI 一侧：记一次新窗口延迟。
 * @param[in] latency_ms 毫秒。
 */
void music_display_stats_note_window(uint32_t latency_ms)
{
    music_display_stats_add(&music_display_stats_window, latency_ms);
}

/**
 * @brief 前台一侧：记下因卷变化而发空窗对应的卡检测边沿时刻。
 * @param[in] edge_tick_ms 边沿时刻（FreeRTOS tick，毫秒）。
 */
void music_display_stats_set_clear_origin(uint32_t edge_tick_ms)
{
    music_display_stats_origin_ms = edge_tick_ms;
    music_display_stats_origin_valid = true;
}

/**
 * @brief GUI 一侧：Queue 已被代次 0 的空窗清空；有记下的边沿时刻就算一次清空延迟并消费它。
 * @param[in] now_ms 当前时刻（FreeRTOS tick，毫秒）。
 */
void music_display_stats_note_clear(uint32_t now_ms)
{
    if (music_display_stats_origin_valid)
    {
        music_display_stats_origin_valid = false;
        music_display_stats_add(&music_display_stats_clear, (uint32_t)(now_ms - music_display_stats_origin_ms));
    }
}

/**
 * @brief 数字截在上限，让汇总行的长度有上界。
 * @param[in] value 数值。
 * @param[in] limit 上限。
 * @return 不超过 limit 的值。
 */
static unsigned long music_display_stats_clamp(uint32_t value, uint32_t limit)
{
    return (unsigned long)((value > limit) ? limit : value);
}

/**
 * @brief 前台任务每圈调一次：距上一行已满一个节拍且有新样本时投一行汇总，并清零本节拍的样本数与最大值。
 * @param[in] now_ms 当前时刻（FreeRTOS tick，毫秒；无符号差处理回绕）。
 * @note  行格式见头文件；数字截在 99999（样本数 9999），整行不超过 127 字符。没有新样本不投，所以稳态不占消息池。
 */
void music_display_stats_poll(uint32_t now_ms)
{
    char line[MUSIC_DISPLAY_STATS_LINE_BYTES];

    if (((uint32_t)(now_ms - music_display_stats_last_report_ms) < (uint32_t)MUSIC_DISPLAY_STATS_PERIOD_MS) ||
        ((music_display_stats_title.Count == 0U) && (music_display_stats_window.Count == 0U) &&
         (music_display_stats_clear.Count == 0U)))
    {
        return;
    }

    (void)snprintf(line, sizeof(line),
                   "title n=%lu max=%lums top=%lums win n=%lu max=%lums top=%lums clear n=%lu max=%lums top=%lums",
                   music_display_stats_clamp(music_display_stats_title.Count, 9999U),
                   music_display_stats_clamp(music_display_stats_title.Max, 99999U),
                   music_display_stats_clamp(music_display_stats_title.Top, 99999U),
                   music_display_stats_clamp(music_display_stats_window.Count, 9999U),
                   music_display_stats_clamp(music_display_stats_window.Max, 99999U),
                   music_display_stats_clamp(music_display_stats_window.Top, 99999U),
                   music_display_stats_clamp(music_display_stats_clear.Count, 9999U),
                   music_display_stats_clamp(music_display_stats_clear.Max, 99999U),
                   music_display_stats_clamp(music_display_stats_clear.Top, 99999U));

    music_display_stats_title.Count = 0U;
    music_display_stats_title.Max = 0U;
    music_display_stats_window.Count = 0U;
    music_display_stats_window.Max = 0U;
    music_display_stats_clear.Count = 0U;
    music_display_stats_clear.Max = 0U;
    music_display_stats_last_report_ms = now_ms;
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, music_display_stats_tag, line);
}

#endif /* MUSIC_DISPLAY_STATS_ENABLE */
