/**
  ******************************************************************************
  * @file    foreground_task.c
  * @brief   前台任务：按阶段施加负载、控制 GUI 睡眠开关、输出 SPIKE 汇总日志（spike #191）。
  *
  * @details
  *          阶段只在音乐处于播放状态时推进计时；暂停或停止时等待。P5 不限时，并且不随播放状态暂停负载：
  *          手动拔卡会让播放停止，但前台任务仍要继续读，才能记下“那次读取返回的状态和时刻”。
  *          负载文件缺失或 Flash 卷未就绪时打一行“未测”日志并跳过该阶段。
  ******************************************************************************
  */

#include "APP/tasks/foreground/foreground_task.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "APP/app_config.h"
#include "APP/tasks/foreground/foreground_load.h"
#include "APP/tasks/foreground/foreground_report.h"
#include "APP/tasks/gui/gui_task.h"
#include "APP/tasks/playback/playback_contract.h"
#include "Middlewares/Third_Party/FatFs/src/ff.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/filesystem/filesystem_spike.h"

/** @brief 汇总窗口：每 10 s 输出一组。 */
#define FOREGROUND_TASK_WINDOW_MS 10000U
/** @brief 等待播放或没有负载时的轮询间隔。 */
#define FOREGROUND_TASK_POLL_MS 100U

/** @brief 一个阶段的定义。 */
typedef struct
{
    const char *Name;                 /**< 日志里的阶段名。 */
    const char *Label;                /**< 阶段说明。 */
    uint32_t DurationMs;              /**< 播放中的时长；0 表示不限。 */
    bool GuiSleep;                    /**< 本阶段 GUI 是否睡眠。 */
    ForegroundLoadKindTypeDef Load;   /**< 前台任务施加的负载。 */
    bool KeepRunningWhenNotPlaying;   /**< 播放暂停或停止时是否继续施加负载。 */
} ForegroundPhaseTypeDef;

static const ForegroundPhaseTypeDef foreground_phases[] = {
    {"P0", "baseline_gui_no_sleep", 30000U, false, FOREGROUND_LOAD_NONE, false},
    {"P1", "gui_sleep", 30000U, true, FOREGROUND_LOAD_NONE, false},
    {"P2", "txt_read", 60000U, true, FOREGROUND_LOAD_TXT, false},
    {"P3", "flash_write_reclaim", 60000U, true, FOREGROUND_LOAD_FLASH, false},
    {"P4", "ui_cell_synthetic", 30000U, true, FOREGROUND_LOAD_CELL, false},
    {"P5", "manual_unplug_switch_page", 0U, true, FOREGROUND_LOAD_TXT, true},
};
#define FOREGROUND_PHASE_COUNT ((uint32_t)(sizeof(foreground_phases) / sizeof(foreground_phases[0])))

/**
 * @brief 开始一个阶段：判断负载能否开始，能则清统计、设 GUI 睡眠开关并输出开始行。
 * @param[in] index 阶段序号。
 * @return true 阶段已开始；false 本阶段“未测”，已输出跳过行，调用者应进入下一阶段。
 */
static bool foreground_task_begin_phase(uint32_t index)
{
    const ForegroundPhaseTypeDef *phase = &foreground_phases[index];
    char detail[80];

    /* 先清统计再开始：Flash 负载在开始时要记下块计数的起点，TXT 负载失败时要记状态。 */
    foreground_load_stats_reset();
    Service_Filesystem_SpikeWindowReset();
    if (!foreground_load_begin(phase->Load, detail, sizeof(detail)))
    {
        foreground_logf("PHASE skip %s %s UNTESTED reason=%s", phase->Name, phase->Label, detail);
        return false;
    }

    gui_task_set_sleep_enabled(phase->GuiSleep);
    foreground_report_baseline();
    foreground_logf("PHASE start %s %s gui_sleep=%s dur=%lums %s", phase->Name, phase->Label,
                    phase->GuiSleep ? "on" : "off", (unsigned long)phase->DurationMs, detail);
    return true;
}

/**
 * @brief 结束一个阶段：输出阶段汇总与结束行，收尾负载。
 * @param[in] index 阶段序号。
 * @param[in] active_ms 阶段内播放状态下经过的毫秒数。
 * @param[in] goal_reached true 为负载达到目标而提前结束，false 为时间到。
 */
static void foreground_task_end_phase(uint32_t index, uint32_t active_ms, bool goal_reached)
{
    const ForegroundPhaseTypeDef *phase = &foreground_phases[index];

    foreground_report_window(phase->Name, phase->Load, true);
    foreground_load_end(phase->Load);
    foreground_logf("PHASE end %s %s active=%lums reason=%s", phase->Name, phase->Label, (unsigned long)active_ms,
                    goal_reached ? "goal_reached" : "time_up");
}

/**
 * @brief 前台任务入口。
 * @param[in] handle 未使用，保留为 FreeRTOS TaskFunction_t 规定的参数。
 * @note  不返回。栈 1536 word，静态创建在 .bss；日志每行之后让出几毫秒，不挤满消息池。
 */
void foreground_task(void *handle)
{
    uint32_t phase_index = 0U;
    uint32_t active_ms = 0U;
    uint32_t window_start_ms = 0U;
    bool phase_running = false;
    bool previous_playing = false;
    bool wait_logged = false;
    TickType_t last_tick;

    (void)handle;

    foreground_load_init();
    foreground_logf("INIT lock=%s fs_lock=%d lfn=%d gui_sleep_default=on fg_stack_words=%u",
                    Service_Filesystem_SpikePerVolume() ? "PER_VOLUME" : "GLOBAL", (int)_FS_LOCK, (int)_USE_LFN,
                    (unsigned)APP_FOREGROUND_TASK_STACK_WORDS);
    last_tick = xTaskGetTickCount();

    for (;;)
    {
        Playback_SnapshotTypeDef snapshot;
        bool playing;
        const TickType_t now = xTaskGetTickCount();
        const uint32_t elapsed_ms = (uint32_t)(now - last_tick) * (uint32_t)portTICK_PERIOD_MS;

        last_tick = now;
        playback_snapshot_read(&snapshot);
        playing = (snapshot.State == PLAYBACK_STATE_PLAYING);
        if ((previous_playing && playing) ||
            (phase_running && (phase_index < FOREGROUND_PHASE_COUNT) &&
             foreground_phases[phase_index].KeepRunningWhenNotPlaying))
        {
            active_ms += elapsed_ms;
        }
        previous_playing = playing;

        foreground_report_unplug();

        if (phase_index >= FOREGROUND_PHASE_COUNT)
        {
            vTaskDelay(pdMS_TO_TICKS(2U * FOREGROUND_TASK_POLL_MS));
            continue;
        }

        if (!phase_running)
        {
            if (!playing)
            {
                if (!wait_logged)
                {
                    foreground_logf("PHASE wait %s music_not_playing", foreground_phases[phase_index].Name);
                    wait_logged = true;
                }
                vTaskDelay(pdMS_TO_TICKS(FOREGROUND_TASK_POLL_MS));
                continue;
            }
            wait_logged = false;
            if (!foreground_task_begin_phase(phase_index))
            {
                phase_index++;
                if (phase_index >= FOREGROUND_PHASE_COUNT)
                {
                    foreground_logf("PHASE all_done");
                }
                continue;
            }
            phase_running = true;
            active_ms = 0U;
            window_start_ms = 0U;
            previous_playing = false;
            last_tick = xTaskGetTickCount();
            continue;
        }

        if (!playing && !foreground_phases[phase_index].KeepRunningWhenNotPlaying)
        {
            if (!wait_logged)
            {
                foreground_logf("PHASE pause %s music_not_playing", foreground_phases[phase_index].Name);
                wait_logged = true;
            }
            vTaskDelay(pdMS_TO_TICKS(FOREGROUND_TASK_POLL_MS));
            continue;
        }
        wait_logged = false;

        foreground_load_step(foreground_phases[phase_index].Load);

        if (((foreground_phases[phase_index].DurationMs != 0U) &&
             (active_ms >= foreground_phases[phase_index].DurationMs)) ||
            foreground_load_finished(foreground_phases[phase_index].Load))
        {
            foreground_task_end_phase(phase_index,
                                      active_ms,
                                      foreground_load_finished(foreground_phases[phase_index].Load));
            phase_running = false;
            phase_index++;
            if (phase_index >= FOREGROUND_PHASE_COUNT)
            {
                foreground_logf("PHASE all_done");
            }
        }
        else if ((active_ms - window_start_ms) >= FOREGROUND_TASK_WINDOW_MS)
        {
            foreground_report_window(foreground_phases[phase_index].Name, foreground_phases[phase_index].Load, false);
            window_start_ms = active_ms;
        }
    }
}
