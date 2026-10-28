/**
 * @file storage_track_diag.h
 * @brief 标签读取诊断：DWT 计时、Storage 私有计数器、名称槽 INFO/WARN 与 Queue 窗口汇总行。
 * @note  只由 Storage Task 的名称槽与窗口槽 load 调用，不进 ISR 与每帧路径（DIAG-3、DIAG-4）。
 *        日志文本只含播放列表位置与原因名，不含文件名、标题或歌手。不改读取行为。
 */

#ifndef STORAGE_TRACK_DIAG_H
#define STORAGE_TRACK_DIAG_H

#include <stdint.h>

#include "storage_track_text.h"

uint32_t storage_track_diag_begin(void);
void storage_track_diag_name_done(uint16_t index, uint32_t start_cycles, const StorageTrackText_ResultTypeDef *track);
void storage_track_diag_window_begin(void);
void storage_track_diag_row_done(uint32_t start_cycles, const StorageTrackText_ResultTypeDef *track);
void storage_track_diag_window_end(void);

#endif /* STORAGE_TRACK_DIAG_H */
