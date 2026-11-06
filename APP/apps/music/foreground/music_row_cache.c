/**
 * @file music_row_cache.c
 * @brief 行缓存：键表在 .bss 里启动清零，行内容放 `.apps_sdram`（NOLOAD，靠键判断有效）。
 */

#include "APP/apps/music/foreground/music_row_cache.h"

#include <string.h>

/** @brief 一行的键：（曲库代次，播放列表位置）；代次 0 表示这一行没有内容。 */
typedef struct
{
    uint32_t Generation;
    uint16_t Position;
} MusicRowCache_KeyTypeDef;

/* 键表：每个取位一个键，启动时清零；只由前台任务访问。 */
static MusicRowCache_KeyTypeDef music_row_cache_keys[MUSIC_ROW_CACHE_LINES];

/* 行内容：放 SDRAM 应用段（约 32 KiB），NOLOAD 不清零，开机时内容无效，键表说了才算；只由前台任务访问。 */
static MusicCells_RowTypeDef music_row_cache_rows[MUSIC_ROW_CACHE_LINES]
    __attribute__((section(MUSIC_SDRAM_SECTION), aligned(32)));

/**
 * @brief 把键表清零（行内容不清）。
 * @note  Boot 任务调用（注册表的 `boot_init`），晚于可选的 SDRAM 自检、早于创建前台任务。
 */
void music_row_cache_boot_init(void)
{
    (void)memset(music_row_cache_keys, 0, sizeof(music_row_cache_keys));
}

/**
 * @brief 取缓存的一行。
 * @param[in] generation 曲库代次；0 永远不命中。
 * @param[in] position 播放列表位置。
 * @param[out] out 命中时写入这一行。
 * @retval true 命中（键与代次、位置都相符）。
 * @retval false 未命中，`out` 不变。
 */
bool music_row_cache_get(uint32_t generation, uint16_t position, MusicCells_RowTypeDef *out)
{
    const size_t slot = (size_t)position % (size_t)MUSIC_ROW_CACHE_LINES;

    if ((generation == 0U) ||
        (music_row_cache_keys[slot].Generation != generation) ||
        (music_row_cache_keys[slot].Position != position))
    {
        return false;
    }

    *out = music_row_cache_rows[slot];
    return true;
}

/**
 * @brief 缓存一行，覆盖同一取位上的旧行。
 * @param[in] generation 曲库代次；0 不缓存。
 * @param[in] position 播放列表位置。
 * @param[in] row 要缓存的行。
 * @note  调用者只缓存结果稳定的行（见头文件）。先写内容、后写键：键是“这一行有效”的唯一依据。
 */
void music_row_cache_put(uint32_t generation, uint16_t position, const MusicCells_RowTypeDef *row)
{
    const size_t slot = (size_t)position % (size_t)MUSIC_ROW_CACHE_LINES;

    if (generation == 0U)
    {
        return;
    }

    music_row_cache_rows[slot] = *row;
    music_row_cache_keys[slot].Generation = generation;
    music_row_cache_keys[slot].Position = position;
}
