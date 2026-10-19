/**
 ******************************************************************************
 * @file    test_playback_task_cursor.c
 * @brief   playback_machine 播放游标的主机行为测试：环形步进与代次绑定。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>

#include "playback_task_test_support.h"

/**
 * @brief 开机后第一次开播，游标从播放列表下标 0 起；开启 OK 把代次由“未确认”换成曲库代次。
 */
static void test_first_open_starts_at_index_zero_and_binds_generation(void)
{
    world_reset();

    world_send(PLAYBACK_COMMAND_TOGGLE);
    assert(world_snapshot().HasTrack);
    assert(world_snapshot().Generation == 0U);
    assert(world_snapshot().Index == 0U);

    world_open_ok(5U, 12U);

    assert(world_snapshot().Generation == 5U);
    assert(world_snapshot().Index == 0U);
}

/**
 * @brief 步进只改下标：NEXT / PREV 的开流请求带游标当前的代次和新位置，代次不变。
 */
static void test_step_requests_carry_cursor_generation_and_new_position(void)
{
    world_reset();
    world_start_playing(5U, 12U);

    world_send(PLAYBACK_COMMAND_NEXT);
    assert(fake_storage_last_request().Generation == 5U);
    assert(fake_storage_last_request().Position == 1U);
    assert(world_snapshot().Generation == 5U);

    world_send(PLAYBACK_COMMAND_PREV);
    world_send(PLAYBACK_COMMAND_PREV);
    assert(fake_storage_last_request().Generation == 5U);
    assert(fake_storage_last_request().Position == 11U);
    assert(world_snapshot().Index == 11U);
    assert(world_snapshot().Generation == 5U);
}

/**
 * @brief 单曲库上一首 / 下一首都回到同一首，但每次都是新流（新序号）。
 */
static void test_single_track_library_wraps_to_itself_with_new_stream(void)
{
    world_reset();
    world_start_playing(5U, 1U);
    uint32_t first_seq = world_current_seq();

    world_send(PLAYBACK_COMMAND_NEXT);
    assert(world_snapshot().Index == 0U);
    uint32_t second_seq = world_current_seq();
    assert(second_seq > first_seq);

    world_send(PLAYBACK_COMMAND_PREV);
    assert(world_snapshot().Index == 0U);
    assert(world_current_seq() > second_seq);
    assert(world_snapshot().Generation == 5U);
}

/**
 * @brief 开启 OK 之前曲目数未知，NEXT / PREV 被忽略且不造出新请求。
 */
static void test_step_before_open_ok_is_ignored_because_track_count_is_unknown(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    uint32_t requests = fake_storage_request_count();

    world_send(PLAYBACK_COMMAND_NEXT);
    world_send(PLAYBACK_COMMAND_PREV);

    assert(fake_storage_request_count() == requests);
    assert(world_snapshot().Index == 0U);
    assert(world_snapshot().Generation == 0U);
    assert(world_snapshot().HandledCommandSeq == 3U);
}

/**
 * @brief SELECT 带 GUI 所见的代次；代次不变时沿用已知曲目数，紧随其后的 NEXT 在开启 OK 之前就能环形步进。
 */
static void test_select_in_same_generation_keeps_track_count_for_following_step(void)
{
    world_reset();
    world_start_playing(5U, 4U);

    world_send_select(5U, 3U);
    world_send(PLAYBACK_COMMAND_NEXT);

    assert(world_snapshot().Index == 0U);
    assert(fake_storage_last_request().Generation == 5U);
    assert(fake_storage_last_request().Position == 0U);
}

/**
 * @brief SELECT 换了代次：游标改绑新代次，请求带新代次；开启 OK 之后用新代次的曲目数环形步进。
 */
static void test_select_with_new_generation_rebinds_cursor_and_track_count(void)
{
    world_reset();
    world_start_playing(5U, 12U);

    world_send_select(9U, 1U);
    assert(world_snapshot().Generation == 9U);
    assert(world_snapshot().Index == 1U);
    assert(fake_storage_last_request().Generation == 9U);
    assert(fake_storage_last_request().Position == 1U);

    world_send(PLAYBACK_COMMAND_NEXT);   /* 新代次的曲目数未知：忽略。 */
    assert(world_snapshot().Index == 1U);

    world_open_ok(9U, 2U);
    world_send(PLAYBACK_COMMAND_NEXT);
    assert(world_snapshot().Index == 0U);   /* 新代次只有 2 首，环回首曲。 */
    assert(fake_storage_last_request().Generation == 9U);
}

/**
 * @brief 重选同一首也分配新流序号，序号严格单调。
 */
static void test_stream_sequence_is_strictly_monotonic_across_switches(void)
{
    world_reset();
    world_start_playing(5U, 3U);

    uint32_t previous = world_current_seq();

    for (uint32_t i = 0U; i < 6U; i++)
    {
        world_send_select(5U, 1U);
        assert(world_current_seq() > previous);
        previous = world_current_seq();
    }
    assert(previous != 0U);
}

/**
 * @brief 运行全部游标测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_first_open_starts_at_index_zero_and_binds_generation();
    test_step_requests_carry_cursor_generation_and_new_position();
    test_single_track_library_wraps_to_itself_with_new_stream();
    test_step_before_open_ok_is_ignored_because_track_count_is_unknown();
    test_select_in_same_generation_keeps_track_count_for_following_step();
    test_select_with_new_generation_rebinds_cursor_and_track_count();
    test_stream_sequence_is_strictly_monotonic_across_switches();

    puts("playback_task_cursor: all tests passed");
    return 0;
}
