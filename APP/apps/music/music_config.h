/**
 * @file music_config.h
 * @brief 音乐应用的固定配置：窗口行数、行缓存、BUSY 重试、链接段名，以及显示延迟汇总和翻页注入钩子的开关。
 *
 * @details
 *          翻页注入测试钩子（DIAG-7、#218、R5.4）：默认关闭，代码写成 `if (MUSIC_HOOK_PAGE_TURN != 0U)`，默认构建也编译；
 *          发布构建不开。打开方式：configure 时 `cmake --preset firmware-debug -DMP3_HOOK_PAGE_TURN=1`，只允许 Debug。
 *          打开后，GUI 音乐分区每次看到新一首开始播放（`TrackStartCount` 变化），就自动写一次翻页需求（窗口起点前进一窗），
 *          让前台任务在块池补满的同时读一窗标签，与音频读取任务争 SD 卷锁；前台任务量块池就绪块数回到水位的耗时并记一行日志。
 */

#ifndef MUSIC_CONFIG_H
#define MUSIC_CONFIG_H

#include "APP/apps/music/catalog/catalog_name.h"

/* music_cells.h：界面数据格子。窗口行数须与 `SERVICE_GUI_MAIN_QUEUE_MAX_ROWS`（Service/gui/main/queue 的配置，不包含 APP 头）同值。 */
#define MUSIC_WINDOW_MAX_ENTRIES   12U    /* 一窗的行数上限；GUI 按窗口的 Length 构造行，不超过此值。 */
#define MUSIC_AXI_SECTION          ".app_axi_ram" /* 格子本体与 GUI 副本所在的链接段：AXI SRAM 新段，NOLOAD，拥有者在首次使用前完整写入（HWD-2）。 */
#define MUSIC_SDRAM_SECTION        ".apps_sdram"  /* 行缓存与组装缓冲所在的链接段：SDRAM，NOLOAD，ASSERT 卡总量 8 MiB（HWD-2、ARC-28）。 */

/* music_row_cache.h */
#define MUSIC_ROW_CACHE_LINES      64U    /* 行缓存行数（配置常量，约 32 KiB）；取位 `位置 mod 行数`，键是（代次，位置），不需要淘汰算法；任何正数都行。 */

/* music_foreground.c：SERVICE_BUSY（文件槽用尽）过约 10 ms 重试，不算文件错误（ARC-27）。 */
#define MUSIC_BUSY_RETRY_MS        10U    /* 两次重试之间至少隔多久，单位为毫秒。 */
#define MUSIC_BUSY_RETRY_MAX       100U   /* 同一行最多重试几次（约 1 s）；用尽后按“打开失败”回退文件主名，不缓存，不再卡住。 */

/* music_display_stats.c：显示延迟汇总（DIAG-6；与 ui_cell 持锁统计同一个开关口径：Debug 开、Release 关）。 */
#ifndef NDEBUG
#define MUSIC_DISPLAY_STATS_ENABLE 1U
#else
#define MUSIC_DISPLAY_STATS_ENABLE 0U
#endif
#define MUSIC_DISPLAY_STATS_PERIOD_MS 10000U /* 汇总日志的节拍：前台任务每圈调一次，距上一行已满此时长且有新样本才投一行（共 1 行）。 */
#define MUSIC_DISPLAY_STATS_LINE_BYTES 128U  /* 汇总行容量（含结尾 NUL）；与 Service Log 单条正文容量相同，数字截在 99999 后最长约 124 字符。 */

/* music_hook_pageturn.c：翻页注入测试钩子（说明见文件头）。 */
#ifndef MUSIC_HOOK_PAGE_TURN
#define MUSIC_HOOK_PAGE_TURN       0U     /* 钩子开关，默认关；构建时 -D 或 CMake 变量 MP3_HOOK_PAGE_TURN=1 打开，发布构建不开。 */
#endif
#define MUSIC_HOOK_PAGE_TURN_TIMEOUT_MS 5000U /* 块池补满的等待上界：超过它就放弃这一次测量并记一行，不无限等。 */
#define MUSIC_HOOK_PAGE_TURN_POLL_MS    1U    /* 测量期间前台任务每隔多久回头量一次块池，单位为毫秒（1 tick）。 */
#define MUSIC_HOOK_PAGE_TURN_LINE_BYTES 112U  /* 钩子日志行容量（含结尾 NUL）。 */

#endif /* MUSIC_CONFIG_H */
