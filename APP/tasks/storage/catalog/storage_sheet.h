/**
 * @file storage_sheet.h
 * @brief Storage Task 的播放列表：Catalog 下标序列，不向 GUI 暴露整表。
 */

#ifndef STORAGE_SHEET_H
#define STORAGE_SHEET_H

#include "APP/tasks/storage/storage_task.h"

#include <stdint.h>

/**
 * @brief 按 Catalog 条数生成顺序播放列表。
 * @param[in] music_indexnum 当前 Catalog 的 IndexNum，即本表有效长度。
 * @param[in] generation 对应的 Catalog 代次；问行时必须与 Catalog 一致。
 */
Storage_StatusTypeDef storage_sheet_init(uint16_t music_indexnum, uint32_t generation);

/**
 * @brief 作废播放列表。只把代次置 0，不清整张下标数组。
 */
Storage_StatusTypeDef storage_sheet_invalidate(void);
uint32_t storage_sheet_generation(void);
uint16_t storage_sheet_catalog_index(uint16_t sheet_index);

#endif /* STORAGE_SHEET_H */