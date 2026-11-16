/**
 * @file music_display_stats.h
 * @brief 显示延迟汇总（R5.1、R5.2）：曲名、新窗口、拔卡后 Queue 清空三类延迟，在固件里算好再输出，只出汇总。
 *
 * @details
 *          延迟用需求序号算（日志时间戳是输出时刻，不能相减，DIAG-6）：
 *          - 曲名：GUI 写曲名需求（带递增序号）到 GUI 采信等于该序号的结果的时间；
 *          - 新窗口：GUI 写窗口需求到 GUI 套用起点等于该需求的窗口的时间；
 *          - 拔卡后 Queue 清空：卡检测边沿（卷任务记下的第一个边沿时刻）到 GUI 套用代次 0 空窗清空 Queue 的时间。
 *          样本由 GUI 一侧记（只加计数、不投日志，不拿锁，可以在帧路径上调用），汇总行由前台任务每圈调 `music_display_stats_poll()` 投，
 *          至多每 10 s 一行、没有新样本不投。走持锁统计的开关：Debug 开、Release 关（`MUSIC_DISPLAY_STATS_ENABLE`），关闭时
 *          下面的入口是空的内联函数，调用点不用写 `#if`。
 *
 *          行格式（标签 `DISPLAY`）：`title n=<本节拍样本数> max=<本节拍最大 ms>ms top=<开机以来最大 ms>ms win n=… max=…ms top=…ms
 *          clear n=… max=…ms top=…ms`，数字截在 99999（样本数截在 9999）；没有样本的一类也列出（n=0 max=0ms）。
 */

#ifndef MUSIC_DISPLAY_STATS_H
#define MUSIC_DISPLAY_STATS_H

#include <stdint.h>

#include "APP/apps/music/music_config.h"

#if (MUSIC_DISPLAY_STATS_ENABLE != 0U)

/* GUI 一侧：一次曲名延迟（毫秒）。 */
void music_display_stats_note_title(uint32_t latency_ms);

/* GUI 一侧：一次新窗口延迟（毫秒）。 */
void music_display_stats_note_window(uint32_t latency_ms);

/* 前台一侧：记下“因卷变了而发空窗”对应的卡检测边沿时刻（FreeRTOS tick，毫秒）；GUI 清空 Queue 时用它算拔卡后清空的延迟。 */
void music_display_stats_set_clear_origin(uint32_t edge_tick_ms);

/* GUI 一侧：Queue 已被代次 0 的空窗清空；有记下的边沿时刻就算一次清空延迟，并消费这个时刻。 */
void music_display_stats_note_clear(uint32_t now_ms);

/* 前台任务每圈调一次：距上一行已满一个节拍且有新样本时投一行汇总，并把本节拍的样本数与最大值清零。 */
void music_display_stats_poll(uint32_t now_ms);

#else

/**
 * @brief 发布构建里显示延迟汇总整段编译掉：什么都不记。
 * @param[in] latency_ms 未使用。
 */
static inline void music_display_stats_note_title(uint32_t latency_ms)
{
    (void)latency_ms;
}

/**
 * @brief 发布构建里显示延迟汇总整段编译掉：什么都不记。
 * @param[in] latency_ms 未使用。
 */
static inline void music_display_stats_note_window(uint32_t latency_ms)
{
    (void)latency_ms;
}

/**
 * @brief 发布构建里显示延迟汇总整段编译掉：什么都不记。
 * @param[in] edge_tick_ms 未使用。
 */
static inline void music_display_stats_set_clear_origin(uint32_t edge_tick_ms)
{
    (void)edge_tick_ms;
}

/**
 * @brief 发布构建里显示延迟汇总整段编译掉：什么都不记。
 * @param[in] now_ms 未使用。
 */
static inline void music_display_stats_note_clear(uint32_t now_ms)
{
    (void)now_ms;
}

/**
 * @brief 发布构建里显示延迟汇总整段编译掉：什么都不投。
 * @param[in] now_ms 未使用。
 */
static inline void music_display_stats_poll(uint32_t now_ms)
{
    (void)now_ms;
}

#endif /* MUSIC_DISPLAY_STATS_ENABLE */

#endif /* MUSIC_DISPLAY_STATS_H */
