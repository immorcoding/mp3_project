/**
  ******************************************************************************
  * @file    app_table.c
  * @brief   应用注册表：一个应用一行。
  *
  * @details
  *          加应用只在这里多一行，并包含该应用的入口头；应用之间不互相包含，
  *          本文件是应用目录里唯一允许认识所有应用的地方。
  ******************************************************************************
  */

#include "app_table.h"

#include "APP/apps/music/gui/gui_music.h"
#include "APP/apps/music/music_app.h"
#include "APP/apps/music/foreground/music_foreground.h"

const AppTable_EntryTypeDef app_table[] =
{
    /* name,    boot_init,       gui_init,        gui_step,        foreground_step */
    {  "music", music_boot_init, gui_music_init,  gui_music_step,  music_foreground_step },
};

const size_t app_table_count = sizeof(app_table) / sizeof(app_table[0]);

/**
 * @brief 对真实注册表逐行调用 `boot_init`（Boot 任务在创建任何任务之前调用）。
 */
void app_table_boot_init(void)
{
    app_table_boot_init_rows(app_table, app_table_count);
}
