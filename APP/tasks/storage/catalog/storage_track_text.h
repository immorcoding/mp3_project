/**
 * @file storage_track_text.h
 * @brief 曲目文本的文件读取封装：打开曲目文件、按偏移读、调曲目文本纯函数、用完即关。
 * @note  只由 Storage Task 调用（名称槽与 Queue 窗口槽的 load），一次只开一个标签读取 FIL。规则见 STOR-15、STOR-16。
 */

#ifndef STORAGE_TRACK_TEXT_H
#define STORAGE_TRACK_TEXT_H

#include <stdbool.h>

#include "storage_catalog_text.h"

/** @brief 一次读取的结果；由本 Module 持有，下一次读取前有效。 */
typedef struct
{
    StorageCatalog_TextTypeDef Text; /**< 曲目显示名、歌手、结果分类与读取次数；打开失败时分类无意义。 */
    bool OpenFailed;                 /**< 打开失败：标题回退文件主名、歌手为空、读取次数为 0。 */
} StorageTrackText_ResultTypeDef;

const StorageTrackText_ResultTypeDef *storage_track_text_read(const char *path);

#endif /* STORAGE_TRACK_TEXT_H */
