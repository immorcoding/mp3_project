/**
 * @file catalog_track_text.h
 * @brief 曲目文本的文件读取封装：按文件定位符打开曲目文件、按偏移读、调曲目文本纯函数、用完即关。
 * @note  只由前台任务的音乐前台流程调用（曲名需求与 Queue 窗口行），路径和定位符来自调用者先做的 `catalog_read_track()`，一次只开一个标签读取 FIL。规则见 STOR-15、STOR-16。
 */

#ifndef CATALOG_TRACK_TEXT_H
#define CATALOG_TRACK_TEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "catalog.h"
#include "catalog_text.h"

/** @brief 一次读取的结果；由本 Module 持有，下一次读取前有效。 */
typedef struct
{
    Catalog_TextTypeDef Text; /**< 曲目显示名、歌手、结果分类与读取次数；打开失败时分类无意义。 */
    bool OpenFailed;                 /**< 打开失败：标题回退文件主名、歌手为空、读取次数为 0。 */
    bool OpenBusy;                   /**< 打开失败的原因是 `SERVICE_BUSY`（文件槽用尽，瞬时的）：调用者过约 10 ms 重试，不当文件错误，不缓存（ARC-27）。仅 OpenFailed 为真时有意义。 */
    uint32_t OpenUs;                 /**< 打开文件的耗时（含核对不符后退回路径打开的那一次），单位 µs，与标签读取耗时分开。 */
} CatalogTrackText_ResultTypeDef;

const CatalogTrackText_ResultTypeDef *catalog_track_text_read(uint16_t position, const Catalog_TrackTypeDef *track);
bool catalog_track_text_last_read_unavailable(void);

#endif /* CATALOG_TRACK_TEXT_H */
