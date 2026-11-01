/**
 * @file storage_catalog_config.h
 * @brief Storage Task catalog/ 分区的固定尺寸：曲库、播放列表窗口。
 */

#ifndef STORAGE_CATALOG_CONFIG_H
#define STORAGE_CATALOG_CONFIG_H

#include "Service/filesystem/filesystem_types.h"

/* storage_catalog.h */
#define STORAGE_CATALOG_MUSIC_POOL_SIZE  (4U * 1024U * 1024U)  /* 曲目 UTF-8 路径字符串池，单位为字节。 */
#define STORAGE_CATALOG_MUSIC_MAX_NUM    12000U  /* 最多曲目数（STOR-17，可同在一个文件夹）；须能放入 uint16_t。每条另随附 16 B 文件定位符，共约 188 KiB。 */

/* storage_catalog.c */
#define STORAGE_CATALOG_DIR_STACK_MAX    128U    /* 待扫子目录栈深度；同时只开一个目录句柄。 */

/* storage_catalog_text.h */
#define STORAGE_CATALOG_TEXT_BLOCK_BYTES 4096U  /* 曲目标签首块与每次后续读取的块大小，初值，由板测修正；也是调用者工作缓冲的最小容量。 */
#define STORAGE_CATALOG_TEXT_MAX_READS   4U     /* 单个文件含首块的最多读取次数，初值，由板测修正。 */

/* storage_track_diag.c：打开耗时的汇总阈值（STOR-17、DIAG-4；初值，由板测修正） */
#define STORAGE_CATALOG_OPEN_BUDGET_US   30000U   /* 窗口汇总触发：单行打开耗时超过此值，单位为微秒（约 30 ms）。 */
#define STORAGE_CATALOG_WINDOW_BUDGET_US 500000U  /* 窗口汇总触发：整窗总耗时超过此值，单位为微秒（约 0.5 s）。 */

/* storage_track_diag.c：单行标签读取的预算阈值（#135 I 节：单行上界 < Storage 单圈余量 T_data/2，板测清单 B8） */
#define STORAGE_CATALOG_TRACK_BUDGET_US  1000000U  /* 单行读取（含打开）耗时的防御阈值，单位为微秒；保留作为窗口汇总的第三个触发条件，须等于 PLAYBACK_MACHINE_DATA_TIMEOUT_MS（T_data）×1000/2，由 Tests/storage_catalog 的编译期断言核对。 */

/* storage_track_diag.c：各类日志行的缓冲容量（含结尾 NUL；超出由 snprintf 截断） */
#define STORAGE_CATALOG_DIAG_TAG_LINE_BYTES     112U  /* 名称槽 Tag 行（INFO 与 WARN）。 */
#define STORAGE_CATALOG_DIAG_WINDOW_LINE_BYTES  128U  /* 窗口汇总行；与 Service_Log 单条文本容量相同，12 行窗口的最长一行约 117 字符，放得下。 */
#define STORAGE_CATALOG_DIAG_LOCATOR_LINE_BYTES 32U   /* Locator miss 行。 */
#define STORAGE_CATALOG_DIAG_STREAM_LINE_BYTES  64U   /* Stream 行。 */

/* storage_listbuffer.h */
#define STORAGE_LISTBUFFER_MAX_ENTRIES   12U    /* 窗口槽位数上限；GUI 按 Length 构造行，不超过此值。 */

#endif /* STORAGE_CATALOG_CONFIG_H */
