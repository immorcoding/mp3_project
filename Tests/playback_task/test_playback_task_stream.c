/**
 ******************************************************************************
 * @file    test_playback_task_stream.c
 * @brief   playback_machine 流交接的主机行为测试：流序号、旧应答与旧块丢弃（B3、B15）、块的持有与归还、
 *          播放中切歌的清理顺序（B11）。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "playback_task_test_support.h"

#define GENERATION 3U
#define TRACKS 6U

/**
 * @brief 开启应答属于被取代的旧流时丢弃：不开播放模块的流，也不改游标代次与曲目数。
 */
static void test_open_reply_of_superseded_stream_is_discarded(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    uint32_t old_seq = world_current_seq();
    world_send_select(GENERATION, 2U);
    assert(world_current_seq() > old_seq);

    fake_storage_open_ok(old_seq, 99U, 40U, WORLD_AUDIO_BYTES);
    world_wake();

    assert(fake_player_begin_count() == 0U);
    assert(world_snapshot().Generation == GENERATION);

    world_open_ok(GENERATION, TRACKS);
    assert(fake_player_begin_count() == 1U);
}

/**
 * @brief 同一条开启应答反复读到时只开一次流（信箱是窥视，不消费）。
 */
static void test_open_reply_is_acted_on_once(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);

    world_open_ok(GENERATION, TRACKS);
    world_wake();
    world_wake();

    assert(fake_player_begin_count() == 1U);
}

/**
 * @brief 就绪队列队首连续的旧序号块取出即归还，从不交给播放模块。
 */
static void test_stale_blocks_at_queue_head_are_returned_without_feeding(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t fed_before = fake_player_fed_total();
    uint32_t returned_before = fake_storage_returned_count();
    uint32_t old_seq = world_current_seq();

    world_send(PLAYBACK_COMMAND_NEXT);
    for (uint32_t i = 0U; i < 5U; i++)
    {
        fake_storage_enqueue_block(old_seq, 20U, 0x80U);
    }
    world_wake();

    assert(fake_storage_returned_count() == returned_before + 5U);
    assert(fake_storage_ready_count() == 0U);
    assert(fake_player_fed_total() == fed_before);
    for (uint32_t i = 0U; i < 5U; i++)
    {
        assert(fake_storage_returned_seq(returned_before + i) == old_seq);
    }
    assert(fake_storage_outstanding() == 0U);
}

/**
 * @brief 旧块后面紧跟新流的块：旧块归还，新块在开启 OK 后照常交入。
 */
static void test_new_stream_blocks_behind_stale_ones_are_fed_after_open_ok(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    uint32_t old_seq = world_current_seq();

    world_send(PLAYBACK_COMMAND_NEXT);
    fake_storage_enqueue_block(old_seq, 20U, 0x80U);
    fake_storage_enqueue_block(old_seq, 20U, 0x80U);
    fake_storage_open_ok(world_current_seq(), GENERATION, TRACKS, WORLD_AUDIO_BYTES);
    fake_storage_enqueue_block(world_current_seq(), 16U, 0x10U);
    uint32_t fed_before = fake_player_fed_total();
    world_wake();

    assert(fake_player_fed_total() == fed_before + 16U);
    assert(fake_player_fed_bytes()[fed_before] == 0x10U);
    assert(fake_player_fed_bytes()[fed_before + 15U] == 0x1FU);
}

/**
 * @brief 流记录没有到达之前，新流的块留在队列里，不交给播放模块。
 */
static void test_current_blocks_wait_in_queue_until_open_ok(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);

    fake_storage_enqueue_block(world_current_seq(), 20U, 0x10U);
    world_wake();

    assert(fake_player_fed_total() == 0U);
    assert(fake_storage_ready_count() == 1U);
    assert(fake_storage_returned_count() == 0U);

    world_open_ok(GENERATION, TRACKS);
    assert(fake_player_fed_total() == 20U);
    assert(fake_storage_ready_count() == 0U);
}

/**
 * @brief 当前块的字节全部拷进窗口后立即归还；多块依序交入，字节顺序与内容保持。
 */
static void test_blocks_are_returned_right_after_copy_and_bytes_keep_order(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(GENERATION, TRACKS);

    fake_storage_enqueue_block(world_current_seq(), 12U, 0U);
    fake_storage_enqueue_block(world_current_seq(), 12U, 100U);
    world_wake();

    assert(fake_player_fed_total() == 24U);
    assert(fake_storage_returned_count() == 2U);
    assert(fake_storage_outstanding() == 0U);
    assert(fake_player_fed_bytes()[0] == 0U);
    assert(fake_player_fed_bytes()[11] == 11U);
    assert(fake_player_fed_bytes()[12] == 100U);
    assert(fake_player_fed_bytes()[23] == 111U);
}

/**
 * @brief 块比窗口大时手中保持这一块，余量在后续唤醒里继续交入；拷完才归还，期间队列里的下一块不被取走。
 */
static void test_oversized_block_is_held_across_wakes_until_fully_copied(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(GENERATION, TRACKS);

    fake_storage_enqueue_block(world_current_seq(), 64U, 0U);   /* 窗口容量 40 B。 */
    fake_storage_enqueue_block(world_current_seq(), 8U, 200U);
    world_wake();
    assert(fake_storage_outstanding() == 1U);
    assert(fake_storage_ready_count() == 1U);

    for (uint32_t i = 0U; (i < 10U) && (fake_storage_ready_count() == 1U); i++)
    {
        world_wake();
    }
    for (uint32_t i = 0U; i < 5U; i++)
    {
        world_wake();
    }

    assert(fake_storage_returned_count() == 2U);
    assert(fake_storage_max_outstanding() == 1U);
    assert(fake_player_fed_total() == 72U);
    for (uint32_t i = 0U; i < 64U; i++)
    {
        assert(fake_player_fed_bytes()[i] == (uint8_t)i);
    }
    assert(fake_player_fed_bytes()[64] == 200U);
    assert(fake_player_fed_bytes()[71] == 207U);
}

/**
 * @brief 播放中切歌的清理顺序（B11）：先写新请求，再让播放模块放弃旧流，再归还手中旧块。
 */
static void test_switching_while_playing_cleans_up_in_the_specified_order(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(GENERATION, TRACKS);
    fake_storage_enqueue_block(world_current_seq(), 64U, 0U);
    world_wake();   /* 手中留着一块未拷完的旧块。 */
    assert(fake_storage_outstanding() == 1U);

    call_log_reset();
    world_send(PLAYBACK_COMMAND_NEXT);

    uint32_t request_at = call_log_index_of(CALL_WRITE_REQUEST, 0U);
    uint32_t abandon_at = call_log_index_of(CALL_ABANDON, 0U);
    uint32_t return_at = call_log_index_of(CALL_RETURN_BLOCK, 0U);

    assert(request_at != UINT32_MAX);
    assert(abandon_at != UINT32_MAX);
    assert(return_at != UINT32_MAX);
    assert(request_at < abandon_at);
    assert(abandon_at < return_at);
    assert(fake_storage_outstanding() == 0U);
    assert(!fake_player_stream_active());
    assert(call_log_count_of(CALL_BEGIN_STREAM) == 0U);
}

/**
 * @brief 切歌后新流的开启 OK 才让播放模块重新开流（⑤），旧窗口的字节不带进新流。
 */
static void test_new_stream_begins_only_after_its_open_ok(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    assert(fake_player_begin_count() == 1U);

    world_send(PLAYBACK_COMMAND_NEXT);
    world_wake();
    assert(fake_player_begin_count() == 1U);

    world_open_ok(GENERATION, TRACKS);
    assert(fake_player_begin_count() == 2U);
    assert(fake_player_stream_active());
}

/**
 * @brief 每次唤醒，不管有没有命令，都归还旧序号块；同一时刻最多持一块。
 */
static void test_at_most_one_block_is_held_while_streaming_many_blocks(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(GENERATION, TRACKS);

    for (uint32_t i = 0U; i < 6U; i++)
    {
        fake_storage_enqueue_block(world_current_seq(), 30U, (uint8_t)(i * 30U));
    }
    for (uint32_t i = 0U; i < 20U; i++)
    {
        world_wake();
        assert(fake_storage_outstanding() <= 1U);
    }

    assert(fake_storage_max_outstanding() == 1U);
    assert(fake_storage_returned_count() == 6U);
    assert(fake_player_fed_total() == 180U);
}

/**
 * @brief 运行全部流交接测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_open_reply_of_superseded_stream_is_discarded();
    test_open_reply_is_acted_on_once();
    test_stale_blocks_at_queue_head_are_returned_without_feeding();
    test_new_stream_blocks_behind_stale_ones_are_fed_after_open_ok();
    test_current_blocks_wait_in_queue_until_open_ok();
    test_blocks_are_returned_right_after_copy_and_bytes_keep_order();
    test_oversized_block_is_held_across_wakes_until_fully_copied();
    test_switching_while_playing_cleans_up_in_the_specified_order();
    test_new_stream_begins_only_after_its_open_ok();
    test_at_most_one_block_is_held_while_streaming_many_blocks();

    puts("playback_task_stream: all tests passed");
    return 0;
}
