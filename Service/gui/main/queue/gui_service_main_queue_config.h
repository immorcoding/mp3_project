/**
  ******************************************************************************
  * @file    gui_service_main_queue_config.h
  * @brief   Queue 行复制的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_QUEUE_CONFIG_H
#define GUI_SERVICE_MAIN_QUEUE_CONFIG_H

/* gui_service_main_queue.c */
#define SERVICE_GUI_MAIN_QUEUE_MAX_ROWS  12U /* 一次构造的行数上限；须与 STORAGE_LISTBUFFER_MAX_ENTRIES 同值。本目录不得包含 APP 头。 */

/* 行排布（#147 方案 B）：行高 = 2 × 字体行高 + 两行间距 + 上下内边距 + 上下边框，按字体行高算，不写死。 */
#define SERVICE_GUI_MAIN_QUEUE_ROW_BORDER     4  /* 左侧边条宽；LVGL 8.3 内容区按四边各减此值。 */
#define SERVICE_GUI_MAIN_QUEUE_ROW_PAD_VER    2  /* 行 Panel 上下内边距。 */
#define SERVICE_GUI_MAIN_QUEUE_ROW_LINE_GAP   2  /* 曲名与歌手两行之间的间距。 */
#define SERVICE_GUI_MAIN_QUEUE_ARTIST_OPA     180 /* 歌手文字 Opa。 */

#endif /* GUI_SERVICE_MAIN_QUEUE_CONFIG_H */
