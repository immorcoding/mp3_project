/**
 * @file music_hook_pageturn.c
 * @brief 翻页注入测试钩子：记注入时刻、量块池补满耗时、记一行日志（默认关闭，说明见 music_hook_pageturn.h）。
 */

#include "APP/apps/music/music_hook_pageturn.h"

#include <stdio.h>

#include "APP/tasks/audio_reader/audio_reader_block.h"
#include "APP/tasks/audio_reader/audio_reader_config.h"
#include "Service/log/log_service.h"

static const char music_hook_pageturn_tag[] = "HOOK";

/* 注入时刻由 GUI 任务写、前台任务读；32 位对齐读写在 Cortex-M7 上不撕裂，valid 最后写、最先清。 */
static volatile uint32_t music_hook_pageturn_start_ms;
static volatile bool music_hook_pageturn_active;

/**
 * @brief 钩子打开时投启动行：`HOOK: page_turn ON watermark=<块数> timeout=<ms>ms`（WARN，DIAG-7：没有 HOOK 行就是没开）。
 * @note  前台任务第一次 step 时调一次；钩子关闭时什么都不做。
 */
void music_hook_pageturn_announce(void)
{
    char line[MUSIC_HOOK_PAGE_TURN_LINE_BYTES];

    if (MUSIC_HOOK_PAGE_TURN != 0U)
    {
        (void)snprintf(line, sizeof(line), "page_turn ON watermark=%lu timeout=%lums",
                       (unsigned long)AUDIO_READER_WATERMARK_BLOCKS,
                       (unsigned long)MUSIC_HOOK_PAGE_TURN_TIMEOUT_MS);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, music_hook_pageturn_tag, line);
    }
}

/**
 * @brief GUI 一侧：翻页需求已写入，记下注入时刻。
 * @param[in] tick_ms 注入时刻（FreeRTOS tick，毫秒）。
 * @note  钩子关闭时什么都不做。只记一个时刻，不拿锁、不投日志，可以在 GUI 帧路径上调用；上一次测量没完成时被新的一次覆盖。
 */
void music_hook_pageturn_note_injection(uint32_t tick_ms)
{
    if (MUSIC_HOOK_PAGE_TURN != 0U)
    {
        music_hook_pageturn_start_ms = tick_ms;
        music_hook_pageturn_active = true;
    }
}

/**
 * @brief 前台任务每圈调一次：有测量在进行时量块池，补满就记一行 INFO，超时记一行 WARN 放弃。
 * @param[in] now_ms 当前时刻（FreeRTOS tick，毫秒）。
 * @return 测量进行时是 `MUSIC_HOOK_PAGE_TURN_POLL_MS`；否则 `UINT32_MAX`（等通知）。
 * @note  “补满”指就绪块数不少于音频读取任务的安全水位（`AUDIO_READER_WATERMARK_BLOCKS`）。行格式（串口里标签后有冒号）：
 *        `I (<tick>) HOOK: page_turn refilled after=<ms>ms ready=<块数> free=<块数>`，超时 `W (<tick>) HOOK: page_turn timeout after=<ms>ms ready=… free=…`。
 *        测量粒度是 1 ms（前台任务每个 tick 回头量一次）。
 */
uint32_t music_hook_pageturn_poll(uint32_t now_ms)
{
    char line[MUSIC_HOOK_PAGE_TURN_LINE_BYTES];
    uint32_t elapsed_ms;
    uint32_t ready;
    uint32_t free_blocks;

    if ((MUSIC_HOOK_PAGE_TURN == 0U) || !music_hook_pageturn_active)
    {
        return UINT32_MAX;
    }

    elapsed_ms = (uint32_t)(now_ms - music_hook_pageturn_start_ms);
    ready = audio_reader_block_ready_count();
    free_blocks = audio_reader_block_free_count();

    if (ready >= (uint32_t)AUDIO_READER_WATERMARK_BLOCKS)
    {
        music_hook_pageturn_active = false;
        (void)snprintf(line, sizeof(line), "page_turn refilled after=%lums ready=%lu free=%lu",
                       (unsigned long)elapsed_ms, (unsigned long)ready, (unsigned long)free_blocks);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, music_hook_pageturn_tag, line);
        return UINT32_MAX;
    }

    if (elapsed_ms >= (uint32_t)MUSIC_HOOK_PAGE_TURN_TIMEOUT_MS)
    {
        music_hook_pageturn_active = false;
        (void)snprintf(line, sizeof(line), "page_turn timeout after=%lums ready=%lu free=%lu",
                       (unsigned long)elapsed_ms, (unsigned long)ready, (unsigned long)free_blocks);
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, music_hook_pageturn_tag, line);
        return UINT32_MAX;
    }

    return (uint32_t)MUSIC_HOOK_PAGE_TURN_POLL_MS;
}
