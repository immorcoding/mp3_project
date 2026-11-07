/**
 * @file catalog_config.h
 * @brief 音乐曲库（`APP/apps/music/catalog/`）的固定尺寸、阈值与持锁统计开关。
 */

#ifndef CATALOG_CONFIG_H
#define CATALOG_CONFIG_H

#include "Service/filesystem/filesystem_types.h"

/* catalog.h */
#define CATALOG_MUSIC_POOL_SIZE  (4U * 1024U * 1024U)  /* 曲目 UTF-8 路径字符串池，单位为字节。 */
#define CATALOG_MUSIC_MAX_NUM    12000U  /* 最多曲目数（STOR-17，可同在一个文件夹）；须能放入 uint16_t。每条另随附 16 B 文件定位符，共约 188 KiB。 */

/* catalog.c */
#define CATALOG_DIR_STACK_MAX    128U    /* 待扫子目录栈深度；同时只开一个目录句柄。栈在扫描遍历状态里，放 `.apps_sdram`（128 × 256 B = 32 KiB）。 */
#define CATALOG_BUSY_RETRY_MS    10U     /* 扫描单步遇到 SERVICE_BUSY（槽用尽）时返回的等待毫秒数：后台任务睡这么久再叫扫描作业（#196 错误分类表，初值）。 */
#define CATALOG_SDRAM_SECTION    ".apps_sdram" /* 扫描遍历状态所在的链接段：SDRAM，NOLOAD，不自动清零，`catalog_boot_init()` 整块写入，ASSERT 卡总量 8 MiB（HWD-2、ARC-28）。 */

/* catalog.c：曲库短锁的持锁统计（DIAG-6；与 Filesystem 的持锁统计同一个开关：Debug 开、Release 关）。
 * 固件 Release 带 -DNDEBUG，整段编译掉，不占时间也不占字段。 */
#ifndef NDEBUG
#define CATALOG_LOCK_STATS_ENABLE        1U
#else
#define CATALOG_LOCK_STATS_ENABLE        0U
#endif
#define CATALOG_LOCK_STATS_PERIOD_MS     10000U  /* 汇总日志的节拍：放锁后发现距上次汇总已满此时长，就投一行（共 1 行）。 */
#define CATALOG_LOCK_STATS_LINE_BYTES    48U     /* 汇总行容量（含结尾 NUL）；`n=<10 位> hold=<10 位>us` 最长 36 字符。 */

/* catalog_text.h */
#define CATALOG_TEXT_BLOCK_BYTES 4096U  /* 曲目标签首块与每次后续读取的块大小，初值，由板测修正；也是调用者工作缓冲的最小容量。 */
#define CATALOG_TEXT_MAX_READS   4U     /* 单个文件含首块的最多读取次数，初值，由板测修正。 */

/* catalog_track_diag.c：打开耗时的汇总阈值（STOR-17、DIAG-4；初值，由板测修正） */
#define CATALOG_OPEN_BUDGET_US   30000U   /* 窗口汇总触发：单行打开耗时超过此值，单位为微秒（约 30 ms）。 */
#define CATALOG_WINDOW_BUDGET_US 500000U  /* 窗口汇总触发：整窗总耗时超过此值，单位为微秒（约 0.5 s）。 */

/* catalog_track_diag.c：单行标签读取的日志阈值 */
#define CATALOG_TRACK_BUDGET_US  1000000U  /* 单行读取（含打开）耗时超过 1 s（初值，单位为微秒）就触发窗口汇总，是窗口汇总的第三个触发条件。 */

/* catalog_track_diag.c：各类日志行的缓冲容量（含结尾 NUL；超出由 snprintf 截断） */
#define CATALOG_DIAG_TAG_LINE_BYTES     112U  /* 曲名 Tag 行（INFO 与 WARN）。 */
#define CATALOG_DIAG_WINDOW_LINE_BYTES  128U  /* 窗口汇总行；与 Service_Log 单条文本容量相同，12 行窗口的最长一行约 117 字符，放得下。 */
#define CATALOG_DIAG_LOCATOR_LINE_BYTES 32U   /* Locator miss 行。 */
#define CATALOG_DIAG_STREAM_LINE_BYTES  64U   /* Stream 行。 */

#endif /* CATALOG_CONFIG_H */
