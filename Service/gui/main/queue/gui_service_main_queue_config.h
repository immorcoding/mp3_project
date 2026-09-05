/**
  ******************************************************************************
  * @file    gui_service_main_queue_config.h
  * @brief   Queue 行复制的私有参数。
  ******************************************************************************
  */

#ifndef GUI_SERVICE_MAIN_QUEUE_CONFIG_H
#define GUI_SERVICE_MAIN_QUEUE_CONFIG_H

/* gui_service_main_queue.c */
#define SERVICE_GUI_MAIN_QUEUE_MAX_ROWS  8U  /* 一次按范本构造的行数上限；须与 STORAGE_LISTBUFFER_MAX_ENTRIES 同值。本目录不得包含 APP 头。 */

#endif /* GUI_SERVICE_MAIN_QUEUE_CONFIG_H */
