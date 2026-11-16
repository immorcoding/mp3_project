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

/* 歌手为空串时歌手 Label 显示的文案，Queue 行与唱盘页共用（GUI-25、GUI-26）。字体与 Opa 由各自 Label 决定：Queue 见 Service/gui/main/queue/gui_service_main_queue_config.h，唱盘页见 Service/gui/gui_service_config.h。 */
#define GUI_MUSIC_TEXT_UNKNOWN_ARTIST "Unknown Artist"

#endif /* GUI_MUSIC_TEXT_H */
