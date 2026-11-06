/**
  ******************************************************************************
  * @file    gui_music_queue_rows.h
  * @brief   音乐分区私有：把窗口结果的两列文本按行整理成 QueueApply 的平行数组。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_QUEUE_ROWS_H
#define GUI_MUSIC_QUEUE_ROWS_H

#include <stdint.h>

#include "APP/apps/music/music_cells.h"

uint16_t gui_music_queue_rows(const MusicCells_WindowTypeDef *window,
                              const char **titles,
                              const char **artists);

#endif /* GUI_MUSIC_QUEUE_ROWS_H */
