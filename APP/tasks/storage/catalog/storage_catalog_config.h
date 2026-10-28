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

/* storage_catalog_text.h */
#define STORAGE_CATALOG_TEXT_BLOCK_BYTES 4096U  /* 曲目标签首块与每次后续读取的块大小，初值，由板测修正；也是调用者工作缓冲的最小容量。 */
#define STORAGE_CATALOG_TEXT_MAX_READS   4U     /* 单个文件含首块的最多读取次数，初值，由板测修正。 */

/* storage_track_diag.c：单行标签读取的预算阈值（#135 I 节：单行上界 < Storage 单圈余量 T_data/2，板测清单 B8） */
#define STORAGE_CATALOG_TRACK_BUDGET_US  1000000U  /* 单行标签读取耗时的超预算阈值，单位为微秒；须等于 PLAYBACK_MACHINE_DATA_TIMEOUT_MS（T_data）×1000/2，由 Tests/storage_catalog 的编译期断言核对。 */

/* storage_listbuffer.h */
#define STORAGE_LISTBUFFER_MAX_ENTRIES   12U    /* 窗口槽位数上限；GUI 按 Length 构造行，不超过此值。 */

#endif /* STORAGE_CATALOG_CONFIG_H */
