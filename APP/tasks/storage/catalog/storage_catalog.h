#ifndef STORAGE_CATALOG_H
#define STORAGE_CATALOG_H

#include <stdint.h>
#include "APP/tasks/storage/storage_task.h"

#define STORAGE_CATALOG_MUSIC_POOL_SIZE (4U * 1024U * 1024U) /**< 4MB 曲目字符串池。 */
#define STORAGE_CATALOG_MUSIC_MAX_NUM   32768U              /**< 最多曲目数量。 */

Storage_StatusTypeDef storage_catalog_music_init(void);
Storage_StatusTypeDef storage_catalog_books_init(void);
Storage_StatusTypeDef storage_catalog_init(void);
Storage_StatusTypeDef storage_catalog_invalidate(void);

#endif /* STORAGE_CATALOG_H */
