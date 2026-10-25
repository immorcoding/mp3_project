/**
  ******************************************************************************
  * @file    track_title_prototype.h
  * @brief   一次性唱盘页曲名原型：三种布局、溢出方式与播放状态演示。
  ******************************************************************************
  */

#ifndef TRACK_TITLE_PROTOTYPE_H
#define TRACK_TITLE_PROTOTYPE_H

#include <stdbool.h>
#include <stdint.h>

void track_title_prototype_init(void);
void track_title_prototype_step(uint32_t elapsed_ms);
bool track_title_prototype_handle_key(char key);
void track_title_prototype_previous_variant(void);
void track_title_prototype_next_variant(void);
void track_title_prototype_previous_state(void);
void track_title_prototype_next_state(void);

#endif /* TRACK_TITLE_PROTOTYPE_H */
