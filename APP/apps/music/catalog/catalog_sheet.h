/**
 * @file catalog_sheet.h
 * @brief 顺序播放列表：播放列表位置到曲库下标的映射，曲库内部使用。
 * @note  不带锁也不带代次：它和曲库同属一把短锁的保护范围。扫描期间曲库代次为 0，读者不碰它，
 *        所以 `catalog_sheet_fill()` 在锁外写；发布后读者经 `catalog_read_track()` 在锁内查 `catalog_sheet_index()`。
 *        调用者只有 `catalog.c`，不属于曲库的公开 Interface。
 */

#ifndef CATALOG_SHEET_H
#define CATALOG_SHEET_H

#include <stdint.h>

/**
 * @brief 按曲库条数生成顺序播放列表（SeqList[i] = i）。
 * @param[in] count 曲库当前条数，即本表有效长度。
 */
void catalog_sheet_fill(uint16_t count);

/**
 * @brief 把播放列表位置映射为曲库下标。
 * @param[in] position 播放列表位置，调用者须已核对它小于曲库条数。
 * @return 曲库下标；位置超出表容量返回 UINT16_MAX。
 */
uint16_t catalog_sheet_index(uint16_t position);

#endif /* CATALOG_SHEET_H */
