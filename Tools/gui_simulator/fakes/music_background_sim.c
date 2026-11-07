/**
 * @file music_background_sim.c
 * @brief 模拟器的音乐后台作业替身：真实注册表（`app_table.c`）里音乐那一行有 `background_step`，模拟器不跑后台任务，
 *        所以只提供这个符号，永远说“没有活，等通知”（扫描由假前台流程里的固定曲库代替）。
 */

#include "APP/apps/music/music_background.h"

#include <stdint.h>

/**
 * @brief  替身：模拟器不跑后台任务，永远说“没有活，等通知”。
 * @return `UINT32_MAX`（即 `BACKGROUND_WAIT_FOREVER`）。
 */
uint32_t music_background_step(void)
{
    return UINT32_MAX; /* BACKGROUND_WAIT_FOREVER */
}
