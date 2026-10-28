/**
 ******************************************************************************
 * @file    test_gui_music_queue_rows.c
 * @brief   Queue 窗口客户把窗口槽两列文本按行交给 QueueApply 的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/gui/music/gui_music_queue_rows.h"

/**
 * @brief 按行写入窗口槽的一行文本。
 * @param[out] window 窗口槽副本。
 * @param[in] row 行号。
 * @param[in] title 曲目显示名。
 * @param[in] artist 歌手；空串表示取不到。
 */
static void put_row(Storage_ListBufferTypeDef *window,
                    uint16_t row,
                    const char *title,
                    const char *artist)
{
    (void)snprintf(window->Rows[row].Title, sizeof(window->Rows[row].Title), "%s", title);
    (void)snprintf(window->Rows[row].Artist, sizeof(window->Rows[row].Artist), "%s", artist);
}

/**
 * @brief 曲名与歌手按行平行交出：第 i 行的曲名与歌手来自窗口槽同一行。
 */
static void test_titles_and_artists_are_parallel(void)
{
    static Storage_ListBufferTypeDef window;
    const char *titles[STORAGE_LISTBUFFER_MAX_ENTRIES];
    const char *artists[STORAGE_LISTBUFFER_MAX_ENTRIES];
    uint16_t length;

    (void)memset(&window, 0, sizeof(window));
    put_row(&window, 0U, "Experience (Einaudi Live)", "Ludovico Einaudi");
    put_row(&window, 1U, "Kiss the Rain", "Yiruma");
    window.Length = 2U;

    length = gui_music_queue_rows(&window, titles, artists);

    assert(length == 2U);
    assert(strcmp(titles[0], "Experience (Einaudi Live)") == 0);
    assert(strcmp(artists[0], "Ludovico Einaudi") == 0);
    assert(strcmp(titles[1], "Kiss the Rain") == 0);
    assert(strcmp(artists[1], "Yiruma") == 0);
}

/**
 * @brief 歌手为空串的行交出界面文案 `Unknown Artist`，不是空串、NULL 或 `--`（GUI-25）；
 *        同窗有歌手的行与曲名不受影响。
 */
static void test_missing_artist_shows_unknown_artist(void)
{
    static Storage_ListBufferTypeDef window;
    const char *titles[STORAGE_LISTBUFFER_MAX_ENTRIES];
    const char *artists[STORAGE_LISTBUFFER_MAX_ENTRIES];
    uint16_t length;

    (void)memset(&window, 0, sizeof(window));
    put_row(&window, 0U, "Clair de Lune", "");
    put_row(&window, 1U, "Experience (Einaudi Live)", "Ludovico Einaudi");
    put_row(&window, 2U, "a.b", "");
    window.Length = 3U;

    length = gui_music_queue_rows(&window, titles, artists);

    assert(length == 3U);
    assert(artists[0] != NULL);
    assert(strcmp(artists[0], "Unknown Artist") == 0);
    assert(strcmp(artists[1], "Ludovico Einaudi") == 0);
    assert(artists[2] != NULL);
    assert(strcmp(artists[2], "Unknown Artist") == 0);
    assert(strcmp(titles[0], "Clair de Lune") == 0);
    assert(strcmp(titles[2], "a.b") == 0);
}

/**
 * @brief 占位只在 GUI 侧：窗口槽里的歌手仍是空串，不被写成 `Unknown Artist`。
 */
static void test_unknown_artist_does_not_touch_window(void)
{
    static Storage_ListBufferTypeDef window;
    const char *titles[STORAGE_LISTBUFFER_MAX_ENTRIES];
    const char *artists[STORAGE_LISTBUFFER_MAX_ENTRIES];

    (void)memset(&window, 0, sizeof(window));
    put_row(&window, 0U, "Kiss the Rain (Live)", "");
    window.Length = 1U;

    assert(gui_music_queue_rows(&window, titles, artists) == 1U);
    assert(strcmp(artists[0], "Unknown Artist") == 0);
    assert(window.Rows[0].Artist[0] == '\0');
}

/**
 * @brief Length 超过容量时只交出容量内的行（GUI-9：行数不超过容量）。
 */
static void test_length_is_clamped_to_capacity(void)
{
    static Storage_ListBufferTypeDef window;
    const char *titles[STORAGE_LISTBUFFER_MAX_ENTRIES];
    const char *artists[STORAGE_LISTBUFFER_MAX_ENTRIES];
    uint16_t i;

    (void)memset(&window, 0, sizeof(window));
    for (i = 0U; i < STORAGE_LISTBUFFER_MAX_ENTRIES; i++)
    {
        put_row(&window, i, "T", "A");
    }
    window.Length = (uint16_t)(STORAGE_LISTBUFFER_MAX_ENTRIES + 5U);

    assert(gui_music_queue_rows(&window, titles, artists) == STORAGE_LISTBUFFER_MAX_ENTRIES);
    assert(strcmp(titles[STORAGE_LISTBUFFER_MAX_ENTRIES - 1U], "T") == 0);
    assert(strcmp(artists[STORAGE_LISTBUFFER_MAX_ENTRIES - 1U], "A") == 0);
}

/**
 * @brief 空窗交出 0 行。
 */
static void test_empty_window_has_no_rows(void)
{
    static Storage_ListBufferTypeDef window;
    const char *titles[STORAGE_LISTBUFFER_MAX_ENTRIES];
    const char *artists[STORAGE_LISTBUFFER_MAX_ENTRIES];

    (void)memset(&window, 0, sizeof(window));
    window.Length = 0U;

    assert(gui_music_queue_rows(&window, titles, artists) == 0U);
}

/**
 * @brief 主机测试入口。
 * @return 0 表示全部通过。
 */
int main(void)
{
    test_titles_and_artists_are_parallel();
    test_missing_artist_shows_unknown_artist();
    test_unknown_artist_does_not_touch_window();
    test_length_is_clamped_to_capacity();
    test_empty_window_has_no_rows();
    (void)printf("gui_music_queue_rows tests passed\n");
    return 0;
}
