/**
  ******************************************************************************
  * @file    gui_music_queue_rows.c
  * @brief   音乐分区私有：把窗口结果的两列文本按行整理成 QueueApply 的平行数组。
  *
  * @details
  *          窗口结果每行只有显示文本 {曲目显示名, 歌手}（STOR-16），本分区不解析路径（GUI-9）；
  *          歌手为空串的行交出界面文案 `Unknown Artist`（GUI-25），不是 `--`；窗口结果本身不改。
  ******************************************************************************
  */

#include "gui_music_queue_rows.h"
#include "gui_music_text.h"

#include <stddef.h>

/**
 * @brief 按 Length 把窗口结果每行的曲目显示名与歌手指针写进两个平行数组。
 * @param[in] window GUI 持有的窗口副本（从结果格整份拷来，持锁只拷内存，GUI 在锁外使用）。
 * @param[out] titles 曲名指针表，容量至少 MUSIC_WINDOW_MAX_ENTRIES。
 * @param[out] artists 歌手指针表，容量同上；第 i 项与 titles[i] 同属窗口第 i 行；
 *             歌手为空串的行指向 GUI_MUSIC_TEXT_UNKNOWN_ARTIST。
 * @return 交出的行数：Length，超过容量时截到 MUSIC_WINDOW_MAX_ENTRIES。
 * @note  指针指向窗口副本内的字符串，只在下一次读结果覆盖副本之前有效；Service 的 Label 会拷贝文本。
 *        Queue 只显示有效窗口的行，所以只按歌手是否为空串判断，没有 `--` 或空白态。
 */
uint16_t gui_music_queue_rows(const MusicCells_WindowTypeDef *window,
                              const char **titles,
                              const char **artists)
{
    uint16_t length;
    uint16_t i;

    length = window->Length;
    if (length > MUSIC_WINDOW_MAX_ENTRIES)
    {
        length = MUSIC_WINDOW_MAX_ENTRIES;
    }

    for (i = 0U; i < length; i++)
    {
        titles[i] = window->Rows[i].Title;
        artists[i] = (window->Rows[i].Artist[0] == '\0') ? GUI_MUSIC_TEXT_UNKNOWN_ARTIST
                                                          : window->Rows[i].Artist;
    }

    return length;
}
