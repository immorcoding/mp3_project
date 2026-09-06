/**
 * @file storage_catalog_config.h
 * @brief Storage Task catalog/ 分区的固定尺寸：曲库、播放列表窗口。
 */

#ifndef STORAGE_CATALOG_CONFIG_H
#define STORAGE_CATALOG_CONFIG_H

#include "Service/filesystem/filesystem_types.h"

/* storage_catalog.h */
#define STORAGE_CATALOG_MUSIC_POOL_SIZE  (4U * 1024U * 1024U)  /* 曲目 UTF-8 路径字符串池，单位为字节。 */
#define STORAGE_CATALOG_MUSIC_MAX_NUM    32000U  /* 最多曲目数；须能放入 uint16_t。 */

/* storage_catalog.c */
#define STORAGE_CATALOG_DIR_STACK_MAX    128U    /* 待扫子目录栈深度；同时只开一个目录句柄。 */

/* storage_listbuffer.h */
#define STORAGE_LISTBUFFER_MAX_ENTRIES   12U    /* 窗口槽位数上限；GUI 按 Length 从范本复制，不超过此值。 */
#define STORAGE_LISTBUFFER_PATH_BYTES    (SERVICE_FILESYSTEM_PATH_MAX_BYTES + 1U)  /* 单行路径拷贝容量，含结尾 '\0'。 */

#endif /* STORAGE_CATALOG_CONFIG_H */
