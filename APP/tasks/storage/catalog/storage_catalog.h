/**
 * @file storage_catalog.h
 * @brief Storage Task 的曲库扫描与作废入口。
 * @note  公开头不暴露字符串池、条目偏移或整表指针。条数上限须能放入 uint16_t。
 */

#ifndef STORAGE_CATALOG_H
#define STORAGE_CATALOG_H

#include <stdint.h>
#include "APP/tasks/storage/storage_task.h"

#define STORAGE_CATALOG_MUSIC_POOL_SIZE (4U * 1024U * 1024U) /**< 4MB 曲目字符串池。 */
#define STORAGE_CATALOG_MUSIC_MAX_NUM   32000U              /**< 最多曲目数；须能放入 uint16_t。 */

Storage_StatusTypeDef storage_catalog_music_init(void);
Storage_StatusTypeDef storage_catalog_books_init(void);
Storage_StatusTypeDef storage_catalog_init(void);
Storage_StatusTypeDef storage_catalog_invalidate(void);

#endif /* STORAGE_CATALOG_H */
