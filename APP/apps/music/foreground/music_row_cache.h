/**
 * @file music_row_cache.h
 * @brief 行缓存：64 行（配置常量），键是（代次，位置），曲名需求和窗口行共用，持有者是音乐前台流程。
 *
 * @details
 *          取位 `位置 mod 行数`，不需要淘汰算法；键是（曲库代次，播放列表位置），代次不符视为未命中，不主动清，
 *          所以重扫之后旧行自然失效。**键表启动时清零**（`music_row_cache_boot_init()`，行内容放 `.apps_sdram`，NOLOAD，
 *          开机时内容无效，靠键判断）。代次 0 不是有效代次，永远不命中。
 *          只缓存结果稳定的行（调用者的约定：读到标签、无 ID3v2、帧缺失、解码失败、超读取上限）；打开失败、读取错误、
 *          `SERVICE_BUSY` 和卷不可用都不缓存，否则瞬时错误会让这首歌在换卡前一直显示文件主名。
 *          只由前台任务访问，没有锁。
 */

#ifndef MUSIC_ROW_CACHE_H
#define MUSIC_ROW_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "APP/apps/music/music_cells.h"

/* Boot 任务调用（注册表的 boot_init 里）：把键表清零。行内容不清，靠键判断有效。 */
void music_row_cache_boot_init(void);

/* 命中则把这一行拷给 out 并返回真；未命中（键不符、代次 0）返回假且不改 out。 */
bool music_row_cache_get(uint32_t generation, uint16_t position, MusicCells_RowTypeDef *out);

/* 缓存一行：覆盖同一取位上的旧行。代次 0 不缓存。 */
void music_row_cache_put(uint32_t generation, uint16_t position, const MusicCells_RowTypeDef *row);

#endif /* MUSIC_ROW_CACHE_H */
