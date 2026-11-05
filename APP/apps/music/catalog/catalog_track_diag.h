/**
 * @file catalog_track_diag.h
 * @brief 标签读取与打开诊断：DWT 计时、Storage 私有计数器、名称槽 INFO/WARN、Queue 窗口汇总行、
 *        Locator miss WARN 与开流 Stream INFO。
 * @note  只由 Storage Task 的名称槽、窗口槽、曲目打开与音频流开启调用，不进 ISR 与每帧路径（DIAG-3、DIAG-4、DIAG-5）。
 *        日志文本只含播放列表位置与原因名，不含文件名、标题或歌手。不改读取行为。
 */

#ifndef CATALOG_TRACK_DIAG_H
#define CATALOG_TRACK_DIAG_H

#include <stdint.h>

#include "catalog_track_text.h"

uint32_t catalog_track_diag_begin(void);
uint32_t catalog_track_diag_elapsed_us(uint32_t start_cycles);
void catalog_track_diag_locator_miss(uint16_t position);
void catalog_track_diag_stream_done(uint16_t position, uint32_t audio_offset, uint32_t open_us, uint32_t lseek_us);
void catalog_track_diag_name_done(uint16_t index, uint32_t start_cycles, const CatalogTrackText_ResultTypeDef *track);
void catalog_track_diag_window_begin(void);
void catalog_track_diag_row_done(uint32_t start_cycles, const CatalogTrackText_ResultTypeDef *track);
void catalog_track_diag_window_end(void);

#endif /* CATALOG_TRACK_DIAG_H */
