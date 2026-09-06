/**
 ******************************************************************************
 * @file    test_storage_playback_cursor.c
 * @brief   播放列表游标的主机行为测试。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "APP/tasks/storage/catalog/storage_playback_cursor.h"

/**
 * @brief 上电未初始化时没有当前曲。
 */
static void test_get_fails_before_init(void)
{
    uint16_t index = 99U;
    uint32_t generation = 99U;

    assert(storage_playback_cursor_get(&index, &generation) == STORAGE_ERROR);
}

/**
 * @brief 空库没有当前曲。
 */
static void test_empty_library_has_no_current(void)
{
    uint16_t index = 99U;
    uint32_t generation = 99U;

    assert(storage_playback_cursor_init(0U, 4U) == STORAGE_OK);
    assert(storage_playback_cursor_get(&index, &generation) == STORAGE_ERROR);
}

/**
 * @brief 代次为 0 时即使条数非空也没有当前曲。
 */
static void test_zero_generation_has_no_current(void)
{
    uint16_t index = 99U;
    uint32_t generation = 99U;

    assert(storage_playback_cursor_init(8U, 0U) == STORAGE_OK);
    assert(storage_playback_cursor_get(&index, &generation) == STORAGE_ERROR);
}

/**
 * @brief 非空库从播放列表下标 0 起，并带上 Catalog 代次。
 */
static void test_nonempty_library_starts_at_zero(void)
{
    uint16_t index = 99U;
    uint32_t generation = 99U;

    assert(storage_playback_cursor_init(12U, 5U) == STORAGE_OK);
    assert(storage_playback_cursor_get(&index, &generation) == STORAGE_OK);
    assert(index == 0U);
    assert(generation == 5U);
}

/**
 * @brief 重扫成功后旧下标作废，非空库重新从 0 起。
 */
static void test_reinit_discards_old_index(void)
{
    uint16_t index = 99U;
    uint32_t generation = 99U;

    assert(storage_playback_cursor_init(12U, 5U) == STORAGE_OK);
    assert(storage_playback_cursor_init(3U, 6U) == STORAGE_OK);
    assert(storage_playback_cursor_get(&index, &generation) == STORAGE_OK);
    assert(index == 0U);
    assert(generation == 6U);
}

/**
 * @brief 作废后没有当前曲。
 */
static void test_invalidate_clears_current(void)
{
    uint16_t index = 99U;
    uint32_t generation = 99U;

    assert(storage_playback_cursor_init(12U, 5U) == STORAGE_OK);
    assert(storage_playback_cursor_invalidate() == STORAGE_OK);
    assert(storage_playback_cursor_get(&index, &generation) == STORAGE_ERROR);
}

/**
 * @brief 输出指针为空时拒绝读取。
 */
static void test_get_rejects_null_outputs(void)
{
    uint16_t index = 0U;
    uint32_t generation = 0U;

    assert(storage_playback_cursor_init(12U, 5U) == STORAGE_OK);
    assert(storage_playback_cursor_get(NULL, &generation) == STORAGE_ERROR);
    assert(storage_playback_cursor_get(&index, NULL) == STORAGE_ERROR);
}

/**
 * @brief 有效库内 set 只改下标，代次不变。
 */
static void test_set_moves_current_within_library(void)
{
    uint16_t index = 99U;
    uint32_t generation = 99U;

    assert(storage_playback_cursor_init(12U, 5U) == STORAGE_OK);
    assert(storage_playback_cursor_set(3U) == STORAGE_OK);
    assert(storage_playback_cursor_get(&index, &generation) == STORAGE_OK);
    assert(index == 3U);
    assert(generation == 5U);
}

/**
 * @brief 下标达到或超过库长时拒绝，保持原当前曲。
 */
static void test_set_rejects_out_of_range(void)
{
    uint16_t index = 99U;
    uint32_t generation = 99U;

    assert(storage_playback_cursor_init(12U, 5U) == STORAGE_OK);
    assert(storage_playback_cursor_set(12U) == STORAGE_ERROR);
    assert(storage_playback_cursor_get(&index, &generation) == STORAGE_OK);
    assert(index == 0U);
    assert(generation == 5U);
}

/**
 * @brief 作废或空库时 set 失败。
 */
static void test_set_fails_when_invalid(void)
{
    assert(storage_playback_cursor_init(0U, 4U) == STORAGE_OK);
    assert(storage_playback_cursor_set(0U) == STORAGE_ERROR);
    assert(storage_playback_cursor_init(12U, 5U) == STORAGE_OK);
    assert(storage_playback_cursor_invalidate() == STORAGE_OK);
    assert(storage_playback_cursor_set(1U) == STORAGE_ERROR);
}

/**
 * @brief 运行全部游标测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_get_fails_before_init();
    test_empty_library_has_no_current();
    test_zero_generation_has_no_current();
    test_nonempty_library_starts_at_zero();
    test_reinit_discards_old_index();
    test_invalidate_clears_current();
    test_get_rejects_null_outputs();
    test_set_moves_current_within_library();
    test_set_rejects_out_of_range();
    test_set_fails_when_invalid();

    puts("storage_playback_cursor_tests: all tests passed");
    return 0;
}
