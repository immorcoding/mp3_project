/**
 * @file music_app.c
 * @brief 音乐应用的 Boot 入口：内存准备（格子、行缓存键表、前台流程状态），做完才创建用到它们的任务。
 */

#include "APP/apps/music/music_app.h"

#include "APP/apps/music/foreground/music_foreground.h"
#include "APP/apps/music/music_cells.h"

/**
 * @brief 注册表的 `boot_init`：Boot 任务在创建 GUI 任务和前台任务之前调用一次。
 * @note  只做内存写入和一次静态互斥量创建，不碰文件接口、不阻塞、不投日志。Boot 任务栈 192 word（#220 从 128 提上来），最深一条是
 *        `music_cells_boot_init()` → `ui_cell_init()` → 创建静态互斥量，核算见 `APP/tasks/foreground/README.md`“Boot 栈核算”。
 */
void music_boot_init(void)
{
    music_cells_boot_init();
    music_foreground_boot_init();
}
