/**
 * @file music_hook_pageturn.h
 * @brief 翻页注入测试钩子（DIAG-7、R5.4）：切歌后自动发一次翻页需求，并记块池补满耗时。默认关闭，只允许 Debug。
 *
 * @details
 *          用于验证“水位与让路”：切歌后音频读取任务在补块（一块 32 KiB，约 35 ms，补到安全水位约 140 ms），这时前台任务读一窗标签
 *          要等 SD 卷锁。钩子让这一幕在切歌后马上发生，不靠人手：
 *          - GUI 音乐分区每次看到新一首开始播放（快照的 `TrackStartCount` 变化），写一次翻页需求（窗口起点前进一窗），并调
 *            `music_hook_pageturn_note_injection()` 记下注入时刻；
 *          - 前台任务每圈调 `music_hook_pageturn_poll()`：量块池就绪块数回到安全水位的耗时，记一行 INFO；超过上界记一行 WARN 放弃。
 *          钩子关闭时（默认）所有函数是空操作，代码写成 `if (MUSIC_HOOK_PAGE_TURN != 0U)`，默认构建也编译。
 *          注入时刻取自 GUI 一帧之内看到新曲的那一刻（GUI 最慢 16 ms 看一次），不是切歌命令被处理的瞬间。
 */

#ifndef MUSIC_HOOK_PAGETURN_H
#define MUSIC_HOOK_PAGETURN_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/apps/music/music_config.h"

/* 前台任务第一次 step 时调一次：钩子打开则投 WARN 启动行 `HOOK: page_turn ON ...`（DIAG-7）；关闭时什么都不做。 */
void music_hook_pageturn_announce(void);

/* GUI 一侧：翻页需求已写入，记下注入时刻（FreeRTOS tick，毫秒）。钩子关闭时什么都不做；只记一个时刻，不拿锁、不投日志。 */
void music_hook_pageturn_note_injection(uint32_t tick_ms);

/* 前台任务每圈调一次：有测量在进行时量块池，补满（就绪块数不少于安全水位）记一行 INFO，超时记一行 WARN；
 * 返回多少毫秒后再叫它：测量进行时是 MUSIC_HOOK_PAGE_TURN_POLL_MS，否则 UINT32_MAX（等通知）。 */
uint32_t music_hook_pageturn_poll(uint32_t now_ms);

#endif /* MUSIC_HOOK_PAGETURN_H */
