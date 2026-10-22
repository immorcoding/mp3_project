/**
 ******************************************************************************
 * @file    test_playback_task_log.c
 * @brief   playback_machine INFO 日志事件的主机行为测试（#108）：事件顺序与文案，停止原因文案与快照一致。
 ******************************************************************************
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "playback_task_test_support.h"

#define GENERATION 5U
#define TRACKS 3U

/**
 * @brief 断言日志恰为期望的文本序列。
 * @param[in] expected 期望文本数组。
 * @param[in] count 期望条数。
 */
static void expect_log(const char *const *expected, uint32_t count)
{
    if (fake_log_count() != count)
    {
        for (uint32_t i = 0U; i < fake_log_count(); i++)
        {
            fprintf(stderr, "log[%u] %s\n", (unsigned)i, fake_log_text(i));
        }
    }
    assert(fake_log_count() == count);
    for (uint32_t i = 0U; i < count; i++)
    {
        assert(strcmp(fake_log_text(i), expected[i]) == 0);
    }
}

/**
 * @brief 播放 → 切歌 → 暂停 → 恢复 → 切歌 → 停止，日志按顺序重建整个过程。
 */
static void test_play_switch_pause_resume_stop_is_reconstructable(void)
{
    static const char *const expected[] = {
        "Start idx=0",
        "Open idx=0 rate=44100",
        "Next idx=1",
        "Open idx=1 rate=44100",
        "Pause",
        "Resume",
        "Prev idx=0",
        "Stop reason=READ_ERROR",
    };

    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    world_send(PLAYBACK_COMMAND_PREV);
    world_open_result(PLAYBACK_MACHINE_OPEN_IO_ERROR);

    expect_log(expected, sizeof(expected) / sizeof(expected[0]));
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_READ_ERROR);
}

/**
 * @brief 停止原因文案与快照里的停止原因枚举名一致（逐个原因核对）。
 */
static void test_stop_reason_text_matches_snapshot_reason(void)
{
    static const struct
    {
        Playback_MachineOpenResultTypeDef Result;
        Playback_StopReasonTypeDef Reason;
        const char *Text;
    } cases[] = {
        {PLAYBACK_MACHINE_OPEN_NOT_READY, PLAYBACK_STOP_REASON_NOT_READY, "Stop reason=NOT_READY"},
        {PLAYBACK_MACHINE_OPEN_STALE, PLAYBACK_STOP_REASON_CATALOG_CHANGED, "Stop reason=CATALOG_CHANGED"},
        {PLAYBACK_MACHINE_OPEN_NO_TRACK, PLAYBACK_STOP_REASON_NO_TRACK, "Stop reason=NO_TRACK"},
        {PLAYBACK_MACHINE_OPEN_IO_ERROR, PLAYBACK_STOP_REASON_READ_ERROR, "Stop reason=READ_ERROR"},
    };

    for (uint32_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        world_reset();
        world_send(PLAYBACK_COMMAND_TOGGLE);
        world_open_result(cases[i].Result);

        assert(world_snapshot().StopReason == cases[i].Reason);
        assert(fake_log_count() == 2U);
        assert(strcmp(fake_log_text(1), cases[i].Text) == 0);
    }
}

/**
 * @brief 音频错误与超时停止的原因文案。
 */
static void test_audio_stop_reasons_are_logged(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_log_clear();
    fake_player_set_fact(FAKE_FACT_TRANSFER_ERROR);
    world_wake();
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_ERROR);
    assert(fake_log_count() == 1U);
    assert(strcmp(fake_log_text(0), "Stop reason=AUDIO_ERROR") == 0);

    world_reset();
    world_send(PLAYBACK_COMMAND_TOGGLE);
    fake_log_clear();
    world_wake_at(world_now() + PLAYBACK_MACHINE_DATA_TIMEOUT_MS);
    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_NO_DATA);
    assert(fake_log_count() == 1U);
    assert(strcmp(fake_log_text(0), "Stop reason=NO_DATA") == 0);
}

/**
 * @brief 坏文件跳过各记一行，整轮都坏停止时附 ALL_BAD。
 */
static void test_bad_file_skip_is_logged(void)
{
    static const char *const expected[] = {
        "Start idx=0",
        "Open idx=0 rate=44100",
        "Next idx=1",
        "Skip bad idx=1",
        "Skip bad idx=2",
        "Skip bad idx=0",
        "Stop reason=ALL_BAD",
    };

    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_NEXT);
    world_open_bad_file();
    world_open_bad_file();
    world_open_bad_file();

    expect_log(expected, sizeof(expected) / sizeof(expected[0]));
}

/**
 * @brief 播完记 Finished，随后的自动前进记 Auto 与新序号。
 */
static void test_stream_end_logs_finished_then_auto_advance(void)
{
    static const char *const expected[] = {
        "Start idx=0",
        "Open idx=0 rate=44100",
        "Finished idx=0",
        "Auto idx=1",
    };

    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_storage_enqueue_block(world_current_seq(), 20U, 1U);
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_ENDED);
    world_wake();
    fake_player_release_tail();
    world_wake();

    expect_log(expected, sizeof(expected) / sizeof(expected[0]));
}

/**
 * @brief 播完后自动前进失败（放弃旧流 Stop 失败）时不记 Auto，只留停止原因。
 */
static void test_auto_advance_failure_does_not_log_auto(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    fake_storage_enqueue_block(world_current_seq(), 20U, 1U);
    fake_storage_set_end(world_current_seq(), PLAYBACK_MACHINE_END_ENDED);
    world_wake();
    fake_player_release_tail();
    fake_player_fail_next_stop();
    world_wake();

    assert(world_snapshot().StopReason == PLAYBACK_STOP_REASON_AUDIO_FAULT);
    for (uint32_t i = 0U; i < fake_log_count(); i++)
    {
        assert(strncmp(fake_log_text(i), "Auto", 4) != 0);
    }
}

/**
 * @brief ResumeOutput 失败时不记 Resume。
 */
static void test_resume_failure_does_not_log_resume(void)
{
    world_reset();
    world_start_playing(GENERATION, TRACKS);
    world_send(PLAYBACK_COMMAND_TOGGLE);
    fake_log_clear();
    fake_player_fail_next_resume();
    world_send(PLAYBACK_COMMAND_TOGGLE);

    assert(fake_log_count() == 0U);
}

/**
 * @brief 被忽略的命令与无变化的唤醒不记日志；SELECT 记 Start。
 */
static void test_ignored_commands_and_idle_wakes_do_not_log(void)
{
    world_reset();
    world_send(PLAYBACK_COMMAND_NEXT);
    world_wake();
    assert(fake_log_count() == 0U);

    world_send_select(GENERATION, 2U);
    assert(fake_log_count() == 1U);
    assert(strcmp(fake_log_text(0), "Start idx=2") == 0);

    world_open_ok(GENERATION, TRACKS);
    world_feed_block(30U);
    uint32_t before = fake_log_count();

    for (uint32_t i = 0U; i < 10U; i++)
    {
        world_wake();
    }
    assert(fake_log_count() == before);
}

/**
 * @brief 运行全部日志测试。
 * @return 成功时返回 0。
 */
int main(void)
{
    test_play_switch_pause_resume_stop_is_reconstructable();
    test_stop_reason_text_matches_snapshot_reason();
    test_audio_stop_reasons_are_logged();
    test_bad_file_skip_is_logged();
    test_stream_end_logs_finished_then_auto_advance();
    test_auto_advance_failure_does_not_log_auto();
    test_resume_failure_does_not_log_resume();
    test_ignored_commands_and_idle_wakes_do_not_log();

    puts("playback_task_log: all tests passed");
    return 0;
}
