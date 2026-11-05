/**
  ******************************************************************************
  * @file    gui_music_queue_rows.h
  * @brief   音乐分区私有：把 READY 窗口槽的两列文本按行整理成 QueueApply 的平行数组。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_QUEUE_ROWS_H
#define GUI_MUSIC_QUEUE_ROWS_H

#include <stdint.h>

#include "APP/tasks/storage/catalog/storage_listbuffer.h"

uint16_t gui_music_queue_rows(const Storage_ListBufferTypeDef *window,
                              const char **titles,
                              const char **artists);

#endif /* GUI_MUSIC_QUEUE_ROWS_H */
