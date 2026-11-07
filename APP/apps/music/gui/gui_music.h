/**
  ******************************************************************************
  * @file    gui_music.h
  * @brief   GUI Task 音乐播放器分区入口。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_H
#define GUI_MUSIC_H

#include "Service/gui/gui_service.h"

void gui_music_init(void);
void gui_music_step(const Service_GUI_InputTypeDef *input);

#endif /* GUI_MUSIC_H */
