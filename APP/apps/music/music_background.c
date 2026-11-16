/**
 * @file music_background.c
 * @brief 音乐应用的后台作业入口：把曲库扫描接到注册表的 `background_step`。
 */

#include "APP/apps/music/music_background.h"

#include "APP/apps/music/catalog/catalog.h"

/**
 * @brief  音乐的后台作业单步：现在只有曲库扫描，直接转交 `catalog_scan_step()`。
 * @return 多少毫秒后再叫它：0 马上，`BACKGROUND_WAIT_FOREVER` 等通知（见 `catalog_scan_step()`）。
 */
uint32_t music_background_step(void)
{
    return catalog_scan_step();
}
