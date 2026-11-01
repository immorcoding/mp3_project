/**
 * @file storage_catalog.h
 * @brief Storage Task 的曲库扫描与作废入口。
 * @note  公开头不暴露字符串池、条目偏移或整表指针。条数上限须能放入 uint16_t。文件定位符对调用者不透明，
 *        只在同一曲库代次内有效（ARC-19）。
 */

#ifndef STORAGE_CATALOG_H
#define STORAGE_CATALOG_H

#include <stdbool.h>
#include <stdint.h>
#include "APP/tasks/storage/storage_task.h"
#include "APP/tasks/storage/catalog/storage_catalog_config.h"
#include "Service/filesystem/filesystem_types.h"

Storage_StatusTypeDef storage_catalog_music_init(void);
Storage_StatusTypeDef storage_catalog_books_init(void);
Storage_StatusTypeDef storage_catalog_init(void);
Storage_StatusTypeDef storage_catalog_invalidate(void);
uint32_t storage_catalog_generation(void);
uint16_t storage_catalog_index_num(void);
Storage_StatusTypeDef storage_catalog_copy_path(uint16_t catalog_index,
                                                char *out,
                                                uint32_t out_bytes);
bool storage_catalog_copy_locator(uint16_t catalog_index,
                                  Service_Filesystem_FileLocatorTypeDef *out);

#endif /* STORAGE_CATALOG_H */
