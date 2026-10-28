/**
  ******************************************************************************
  * @file    gui_music_text.h
  * @brief   音乐分区私有：曲目文本的界面占位文案，集中定义一处。
  *
  * @details
  *          只放“取不到曲目元数据时显示什么”的界面文案；Storage 与窗口行内容不变，
  *          占位只在 GUI 侧替换（GUI-25）。0.8.2 唱盘页歌手行复用同一份。
  ******************************************************************************
  */

#ifndef GUI_MUSIC_TEXT_H
#define GUI_MUSIC_TEXT_H

/* 歌手为空串时 Queue 歌手 Label 显示的文案（GUI-25）。样式同歌手 Label，字体与 Opa 见 Service/gui/main/queue/gui_service_main_queue_config.h 与该目录 README。 */
#define GUI_MUSIC_TEXT_UNKNOWN_ARTIST "Unknown Artist"

#endif /* GUI_MUSIC_TEXT_H */
