/**
  ******************************************************************************
  * @file    gui_music_queue_rows.c
  * @brief   音乐分区私有：把 READY 窗口槽的两列文本按行整理成 QueueApply 的平行数组。
  *
  * @details
  *          窗口槽每行只有显示文本 {曲目显示名, 歌手}（STOR-16），本分区不解析路径（GUI-9）；
  *          歌手取不到时该行是空串，原样交给 Service（GUI-25），不替换成 `--`。
  ******************************************************************************
  */

#include "gui_music_queue_rows.h"

#include <stddef.h>

/**
 * @brief 按 Length 把窗口槽每行的曲目显示名与歌手指针写进两个平行数组。
 * @param[in] window 状态为 READY 的窗口槽；READY 期间 Storage 不再写槽。
 * @param[out] titles 曲名指针表，容量至少 STORAGE_LISTBUFFER_MAX_ENTRIES。
 * @param[out] artists 歌手指针表，容量同上；第 i 项与 titles[i] 同属窗口第 i 行。
 * @return 交出的行数：Length，超过容量时截到 STORAGE_LISTBUFFER_MAX_ENTRIES。
 * @note  指针指向窗口槽内的字符串，只在把槽写回 IDLE 之前有效；Service 的 Label 会拷贝文本。
 */
uint16_t gui_music_queue_rows(const Storage_ListBufferTypeDef *window,
                              const char **titles,
                              const char **artists)
{
    uint16_t length;
    uint16_t i;

    length = window->Length;
    if (length > STORAGE_LISTBUFFER_MAX_ENTRIES)
    {
        length = STORAGE_LISTBUFFER_MAX_ENTRIES;
    }

    for (i = 0U; i < length; i++)
    {
        titles[i] = window->Rows[i].Title;
        artists[i] = window->Rows[i].Artist;
    }

    return length;
}
