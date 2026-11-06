/**
 ******************************************************************************
 * @file    test_audio_reader_pace.c
 * @brief   音频读取任务“决定等多久”的纯函数：就绪块不到水位不等，够水位且还有空闲块让 1 tick，
 *          没有空闲块或没有打开的流一直等；新请求和 SERVICE_BUSY 的重试时刻优先（#215，#194 第 2、4 条）。
 *
 * @details
 *          期望值来自 #194 的表和 #202 D 节，不用代码里的表达式重算：水位 4 块、水位以上每读一块让 1 tick、
 *          “一直等”用 AUDIO_READER_WAIT_FOREVER。水位按就绪队列里的块数算，不含播放任务手里那块，这由调用者
 *          传入的 ReadyBlocks 保证（就绪队列的块数），不在本函数里减。
 ******************************************************************************
 */

#include "host_test_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP/tasks/audio_reader/audio_reader_pace.h"

/**
 * @brief 构造一个流已打开的输入。
 * @param[in] ready 就绪队列里的块数。
 * @param[in] free_blocks 空闲队列里的块数。
 * @return 其余字段为假或 0 的输入。
 */
static AudioReader_PaceInputTypeDef open_stream(uint32_t ready, uint32_t free_blocks)
{
    AudioReader_PaceInputTypeDef input;

    memset(&input, 0, sizeof(input));
    input.StreamOpen = true;
    input.ReadyBlocks = ready;
    input.FreeBlocks = free_blocks;
    return input;
}

/**
 * @brief 没有打开的流（从未开启、读完、读错、拔卡停住）：一直等，叫醒它的只有新请求、块归还、卷事件。
 */
static void test_no_open_stream_waits_forever(void)
{
    AudioReader_PaceInputTypeDef input = open_stream(0U, 32U);

    input.StreamOpen = false;
    assert(audio_reader_wait_ticks(&input) == AUDIO_READER_WAIT_FOREVER);
    input.ReadyBlocks = 31U;
    input.FreeBlocks = 1U;
    assert(audio_reader_wait_ticks(&input) == AUDIO_READER_WAIT_FOREVER);
}

/**
 * @brief 没有空闲块（暂停时块池满了，流保持打开）：一直等，块归还会叫醒；就绪块再少也一样。
 */
static void test_no_free_block_waits_forever_even_below_the_watermark(void)
{
    AudioReader_PaceInputTypeDef input = open_stream(0U, 0U);

    assert(audio_reader_wait_ticks(&input) == AUDIO_READER_WAIT_FOREVER);
    input = open_stream(32U, 0U);
    assert(audio_reader_wait_ticks(&input) == AUDIO_READER_WAIT_FOREVER);
}

/**
 * @brief 就绪块不到水位（4 块）：不等，连续补块。0 到 3 块都是。
 */
static void test_below_the_watermark_does_not_wait(void)
{
    for (uint32_t ready = 0U; ready < 4U; ready++)
    {
        AudioReader_PaceInputTypeDef input = open_stream(ready, 32U - ready);

        assert(audio_reader_wait_ticks(&input) == 0U);
    }
}

/**
 * @brief 就绪块够水位且还有空闲块：每读一块让 1 tick（让路用这个等待点）。正好 4 块就让。
 */
static void test_at_or_above_the_watermark_yields_one_tick(void)
{
    AudioReader_PaceInputTypeDef input = open_stream(4U, 28U);

    assert(audio_reader_wait_ticks(&input) == 1U);
    input = open_stream(5U, 27U);
    assert(audio_reader_wait_ticks(&input) == 1U);
    input = open_stream(31U, 1U);
    assert(audio_reader_wait_ticks(&input) == 1U);
}

/**
 * @brief 信箱里已经有新请求：不等，哪怕块池满了或没有打开的流（取消和新流不能被让路或空等挡住）。
 */
static void test_a_pending_request_never_waits(void)
{
    AudioReader_PaceInputTypeDef input = open_stream(32U, 0U);

    input.RequestPending = true;
    assert(audio_reader_wait_ticks(&input) == 0U);
    input = open_stream(4U, 28U);
    input.RequestPending = true;
    assert(audio_reader_wait_ticks(&input) == 0U);
    input.StreamOpen = false;
    assert(audio_reader_wait_ticks(&input) == 0U);
}

/**
 * @brief SERVICE_BUSY 的重试：没到时刻就只等到时刻（约 10 ms），哪怕没有打开的流（开流重试）或就绪块不到水位
 *        （读块重试，否则会空转）；到了时刻不等。新请求仍然优先。
 */
static void test_a_busy_retry_waits_until_it_is_due(void)
{
    AudioReader_PaceInputTypeDef input = open_stream(0U, 32U);

    input.RetryActive = true;
    input.RetryRemainingTicks = 7U;
    assert(audio_reader_wait_ticks(&input) == 7U);

    input.StreamOpen = false;                       /* 开流重试：没有打开的流，不能“一直等”。 */
    assert(audio_reader_wait_ticks(&input) == 7U);

    input.RetryRemainingTicks = 0U;                 /* 到时刻了：马上重试。 */
    assert(audio_reader_wait_ticks(&input) == 0U);

    input.RetryRemainingTicks = 7U;
    input.RequestPending = true;                    /* 新请求取代旧请求，不等重试时刻。 */
    assert(audio_reader_wait_ticks(&input) == 0U);
}

/**
 * @brief 运行全部用例。
 * @return 0 表示全部通过（失败由 assert 中止）。
 */
int main(void)
{
    test_no_open_stream_waits_forever();
    test_no_free_block_waits_forever_even_below_the_watermark();
    test_below_the_watermark_does_not_wait();
    test_at_or_above_the_watermark_yields_one_tick();
    test_a_pending_request_never_waits();
    test_a_busy_retry_waits_until_it_is_due();
    puts("audio_reader_pace: all passed");
    return 0;
}
