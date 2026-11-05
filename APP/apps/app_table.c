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

const AppTable_EntryTypeDef app_table[] =
{
    /* gui_init,        gui_step */
    {  gui_music_init,  gui_music_step },
};

const size_t app_table_count = sizeof(app_table) / sizeof(app_table[0]);
