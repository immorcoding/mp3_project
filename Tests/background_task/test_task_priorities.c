/**
 ******************************************************************************
 * @file    test_task_priorities.c
 * @brief   任务优先级排序的编译期核对（ADR-0023、#220）：期望值是 #202 A 节的排序和用户 2026-11-07 的决定
 *          （Flash 启动任务 2 → 1，与后台任务同级），用字面量写，不用配置宏重算。
 *
 * @details
 *          排序：播放 4 > 音频读取、卷 3 > 前台、GUI、Log、Monitor 2 > 后台、Flash 启动 1 > Boot 0；
 *          `configMAX_PRIORITIES` 是 5（不改 FreeRTOSConfig.h），所以最大优先级 4。
 *          Flash 启动任务与后台任务同级，是为了让开机前约 8 s 的 `MountFlash`（CPU 占约 77 %）不再抢走曲库扫描的 CPU：
 *          同级时间片轮转，扫描不再几乎轮不上（第 5 次上板：大目录开机扫描 9.0 s）。它仍低于卷任务，`MountFlash` 期间插拔卡照常响应；
 *          仍低于前台，前台读标签的步不会被它拖住。
 *          这是编译期核对，主机上没有调度器：不证明真实的 CPU 份额，开机扫描时长靠第 6 次上板（清单 3 的开机扫描一行）。
 ******************************************************************************
 */

#include <stdio.h>

#include "APP/app_config.h"

_Static_assert(APP_PLAYBACK_TASK_PRIORITY == 4U, "playback is the highest application priority (configMAX_PRIORITIES is 5)");
_Static_assert(APP_AUDIO_READER_TASK_PRIORITY == 3U, "the audio reader shares priority 3 with the volume task");
_Static_assert(APP_VOLUME_TASK_PRIORITY == 3U, "the volume task shares priority 3 with the audio reader");
_Static_assert(APP_FOREGROUND_TASK_PRIORITY == 2U, "foreground is priority 2");
_Static_assert(APP_GUI_TASK_PRIORITY == 2U, "GUI is priority 2");
_Static_assert(APP_LOG_TASK_PRIORITY == 2U, "Log is priority 2");
_Static_assert(APP_MONITOR_TASK_PRIORITY == 2U, "Monitor is priority 2");
_Static_assert(APP_BACKGROUND_TASK_PRIORITY == 1U, "background is priority 1");
_Static_assert(APP_FLASH_BOOT_TASK_PRIORITY == 1U,
               "the Flash boot task shares priority 1 with the background task so MountFlash cannot starve the catalog scan (#220)");
_Static_assert(APP_BOOT_TASK_PRIORITY == 0U, "Boot is the lowest priority");
_Static_assert(APP_FLASH_BOOT_TASK_PRIORITY < APP_VOLUME_TASK_PRIORITY,
               "the Flash boot task stays below the volume task so card insertion and removal are handled during MountFlash");
_Static_assert(APP_FLASH_BOOT_TASK_PRIORITY < APP_FOREGROUND_TASK_PRIORITY,
               "the Flash boot task stays below the foreground task so foreground steps are never held up by MountFlash");

/* Boot 栈 192 word（#220，用户 2026-11-06 的决定，从 128 word 提上来）。 */
_Static_assert(APP_BOOT_TASK_STACK_WORDS == 192U, "the Boot task stack is 192 words (768 B)");

/**
 * @brief 运行：全部核对都在编译期，这里只证明可执行文件链接并运行过。
 * @return 0。
 */
int main(void)
{
    puts("task_priorities: ok");
    return 0;
}
