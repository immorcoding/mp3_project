/**
 ******************************************************************************
 * @file    test_music_row_cache.c
 * @brief   行缓存的主机行为测试（STOR-16、#193）：64 行、键是（代次，位置）、取位 `位置 mod 64`、键表启动时清零。
 *
 * @details
 *          只经 `music_row_cache.h` 的公开函数观察。期望值写成字面量（来自 #193：“64 行，取位位置 mod 64，键是（代次，位置），
 *          代次不符视为未命中，不主动清，键表启动时清零”）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "APP/apps/music/foreground/music_row_cache.h"

/**
 * @brief 构造一行。
 * @param[out] row 行。
 * @param[in] title 曲名。
 * @param[in] artist 歌手。
 */
static void make_row(MusicCells_RowTypeDef *row, const char *title, const char *artist)
{
    (void)memset(row, 0, sizeof(*row));
    (void)strncpy(row->Title, title, sizeof(row->Title) - 1U);
    (void)strncpy(row->Artist, artist, sizeof(row->Artist) - 1U);
}

/** @brief 启动后键表是空的：任何（代次，位置）都不命中，也不改调用者的缓冲。 */
static void test_boot_init_leaves_nothing_cached(void)
{
    MusicCells_RowTypeDef out;

    make_row(&out, "untouched", "untouched");
    music_row_cache_boot_init();

    assert(!music_row_cache_get(1U, 0U, &out));
    assert(!music_row_cache_get(7U, 63U, &out));
    assert(strcmp(out.Title, "untouched") == 0);
}

/** @brief 存进去的行按（代次，位置）取得到，曲名和歌手一起。 */
static void test_put_then_get_returns_the_same_row(void)
{
    MusicCells_RowTypeDef row;
    MusicCells_RowTypeDef out;

    music_row_cache_boot_init();
    make_row(&row, "Title 5", "Artist 5");
    music_row_cache_put(3U, 5U, &row);

    assert(music_row_cache_get(3U, 5U, &out));
    assert(strcmp(out.Title, "Title 5") == 0);
    assert(strcmp(out.Artist, "Artist 5") == 0);
}

/** @brief 代次不符视为未命中：同一位置换了代次（重扫）就读不到旧行，也不需要任何清除动作。 */
static void test_a_different_generation_is_a_miss(void)
{
    MusicCells_RowTypeDef row;
    MusicCells_RowTypeDef out;

    music_row_cache_boot_init();
    make_row(&row, "old card", "");
    music_row_cache_put(3U, 5U, &row);

    assert(!music_row_cache_get(4U, 5U, &out));
    assert(music_row_cache_get(3U, 5U, &out)); /* 旧代次的行还在，只是新代次读不到。 */
}

/** @brief 位置不符视为未命中。 */
static void test_a_different_position_is_a_miss(void)
{
    MusicCells_RowTypeDef row;
    MusicCells_RowTypeDef out;

    music_row_cache_boot_init();
    make_row(&row, "five", "");
    music_row_cache_put(3U, 5U, &row);

    assert(!music_row_cache_get(3U, 6U, &out));
}

/** @brief 取位是位置 mod 64：位置 5 和位置 69 共用一行，后存的覆盖先存的（不需要淘汰算法）。 */
static void test_positions_64_apart_share_a_line_and_the_later_put_wins(void)
{
    MusicCells_RowTypeDef row;
    MusicCells_RowTypeDef out;

    music_row_cache_boot_init();
    make_row(&row, "position 5", "");
    music_row_cache_put(3U, 5U, &row);
    make_row(&row, "position 69", "");
    music_row_cache_put(3U, 69U, &row);

    assert(!music_row_cache_get(3U, 5U, &out));
    assert(music_row_cache_get(3U, 69U, &out));
    assert(strcmp(out.Title, "position 69") == 0);
}

/** @brief 64 个相邻位置各占一行，互不覆盖：一窗 12 行和滑动用的几十行都能同时留住。 */
static void test_64_consecutive_positions_all_stay_cached(void)
{
    MusicCells_RowTypeDef row;
    MusicCells_RowTypeDef out;
    char text[16];

    music_row_cache_boot_init();
    for (uint16_t position = 100U; position < 164U; position++)
    {
        (void)snprintf(text, sizeof(text), "t%u", (unsigned)position);
        make_row(&row, text, "");
        music_row_cache_put(9U, position, &row);
    }

    for (uint16_t position = 100U; position < 164U; position++)
    {
        (void)snprintf(text, sizeof(text), "t%u", (unsigned)position);
        assert(music_row_cache_get(9U, position, &out));
        assert(strcmp(out.Title, text) == 0);
    }
}

/** @brief 代次 0 不是有效代次：不缓存，也永远不命中（启动清零的键表就是代次 0）。 */
static void test_generation_zero_is_never_cached_or_hit(void)
{
    MusicCells_RowTypeDef row;
    MusicCells_RowTypeDef out;

    music_row_cache_boot_init();
    make_row(&row, "gen zero", "");
    music_row_cache_put(0U, 0U, &row);

    assert(!music_row_cache_get(0U, 0U, &out));
}

/** @brief 再次启动清键表：之前缓存的行不再命中。 */
static void test_boot_init_clears_the_key_table(void)
{
    MusicCells_RowTypeDef row;
    MusicCells_RowTypeDef out;

    music_row_cache_boot_init();
    make_row(&row, "cached", "");
    music_row_cache_put(3U, 5U, &row);
    assert(music_row_cache_get(3U, 5U, &out));

    music_row_cache_boot_init();
    assert(!music_row_cache_get(3U, 5U, &out));
}

/** @brief 行缓存是 64 行（配置常量）。 */
static void test_line_count_is_64(void)
{
    assert(MUSIC_ROW_CACHE_LINES == 64U);
}

/**
 * @brief 主机测试入口。
 * @return 0 表示全部通过。
 */
int main(void)
{
    test_boot_init_leaves_nothing_cached();
    test_put_then_get_returns_the_same_row();
    test_a_different_generation_is_a_miss();
    test_a_different_position_is_a_miss();
    test_positions_64_apart_share_a_line_and_the_later_put_wins();
    test_64_consecutive_positions_all_stay_cached();
    test_generation_zero_is_never_cached_or_hit();
    test_boot_init_clears_the_key_table();
    test_line_count_is_64();
    puts("music_row_cache: OK");
    return 0;
}
