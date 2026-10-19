/**
 * @file test_playback_service_output.c
 * @brief 播放模块输出管线主路径：预填与启动、半区补满、流尾放完、短文件与补静音。
 * @note  真实 minimp3 + 小 MP3 夹具 + 假 Platform 音频（fake_platform_audio.h）；只经 playback_service.h 与
 *        platform_audio.h 的公开接口观察行为。期望 PCM 来自解码半边的 Advance/ReadFrame（独立于输出路径）。
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "Tests/playback_service/fake_platform_audio.h"
#include "Tests/playback_service/playback_service_test_support.h"

#define HALF_FRAMES (PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF)

/**
 * @brief 第一次推进预填两个半区并 Start：采样率取自首帧，半区内容就是参考 PCM 的头两个半区。
 * @note  Start 把两个半区视为已提交，所以预填阶段不调用 SubmitHalf；环形缓冲首地址满足 32 B 对齐。
 */
static void test_first_pump_prefills_two_halves_and_starts_output(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    test_decode_reference(blob.Data, blob.Size, (uint32_t)blob.Size, &ref);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));

    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);

    assert(report.OutputStarted);
    assert(report.FirstFrame);
    assert(report.StreamInfo.SampleRateHz == 44100U);
    assert(!report.NeedInput && !report.SyncExhausted && !report.StartRejected && !report.StreamEnded);
    assert(g_fake_audio.StartCount == 1U);
    assert(g_fake_audio.StartRate == 44100U);
    assert(g_fake_audio.StartFrames == HALF_FRAMES);
    assert((((uintptr_t)g_fake_audio.StartRing) % 32U) == 0U);
    assert(g_fake_audio.SubmitCount == 0U);
    assert(g_fake_audio.StopCount == 0U);
    assert(memcmp(g_fake_audio.StartSnapshot, ref.Pcm, (size_t)HALF_FRAMES * 2U * 2U * sizeof(int16_t)) == 0);
    assert(report.DecodedMs == (2U * HALF_FRAMES * 1000U) / 44100U);
    assert(g_fake_audio.ContractViolations == 0U);

    test_free_pcm(&ref);
    test_free_blob(&blob);
}

/**
 * @brief 输入不够预填两个半区时不 Start，回报需要输入；补足字节后再推进才 Start，且预填内容不丢。
 * @note  只给 1000 B（不足一帧窗口门槛），再给其余。
 */
static void test_prefill_waits_for_input_then_starts_without_losing_audio(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    test_decode_reference(blob.Data, blob.Size, (uint32_t)blob.Size, &ref);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));
    feeder.Limit = 1000U;

    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.NeedInput);
    assert(!report.OutputStarted);
    assert(g_fake_audio.StartCount == 0U);

    feeder.Limit = 0U;
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.OutputStarted);
    assert(!report.NeedInput);
    assert(memcmp(g_fake_audio.StartSnapshot, ref.Pcm, (size_t)HALF_FRAMES * 2U * 2U * sizeof(int16_t)) == 0);

    test_free_pcm(&ref);
    test_free_blob(&blob);
}

/**
 * @brief 半区释放后下一次推进补满并提交该半区，内容是紧接着的下一段音频，并回报输出有进展。
 * @note  第 1 次释放的是半区 0（奇偶约定），补进去的是参考 PCM 的第 3 个半区。
 */
static void test_released_half_is_refilled_with_next_audio_and_submitted(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    test_decode_reference(blob.Data, blob.Size, (uint32_t)blob.Size, &ref);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);

    /* 没有新的释放时再推进：不提交、不报进展。 */
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(!report.OutputProgress);
    assert(g_fake_audio.SubmitCount == 0U);

    assert(fake_audio_release_half() == 1);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.OutputProgress);
    assert(!report.OutputStarted);
    assert(g_fake_audio.SubmitCount == 1U);
    assert(g_fake_audio.Calls[g_fake_audio.CallCount - 1U].Kind == FAKE_AUDIO_CALL_SUBMIT);
    assert(g_fake_audio.Calls[g_fake_audio.CallCount - 1U].Arg == 0U);
    assert(memcmp(fake_audio_clean_half(0U), ref.Pcm + ((size_t)2U * HALF_FRAMES * 2U), (size_t)HALF_FRAMES * 2U * sizeof(int16_t)) == 0);

    assert(fake_audio_release_half() == 1);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(g_fake_audio.Calls[g_fake_audio.CallCount - 1U].Arg == 1U);
    assert(memcmp(fake_audio_clean_half(1U), ref.Pcm + ((size_t)3U * HALF_FRAMES * 2U), (size_t)HALF_FRAMES * 2U * sizeof(int16_t)) == 0);
    assert(g_fake_audio.OwnershipViolations == 0U);

    test_free_pcm(&ref);
    test_free_blob(&blob);
}

/**
 * @brief 每个 44.1 kHz 夹具、无论分块多碎，都按序放出全部音频，一帧不丢不重，然后回报流尾放完并 Stop 一次。
 * @note  这些夹具的 PCM 总帧数恰为 1152 的整数倍：最后一个数据半区是整半区，其后下一半区是静音。
 *        放出的总帧数等于参考帧数——最后一个数据半区释放时即 Stop，静音半区还没被读。
 */
static void test_every_44k_fixture_plays_through_in_order_and_reports_stream_end(void)
{
    static const char *const names[] = {"cbr_44100_stereo.mp3", "vbr_xing_44100_stereo.mp3", "vbri_44100_stereo.mp3"};
    static const uint32_t chunks[] = {0U, 313U};
    size_t f;
    size_t c;

    for (f = 0U; f < (sizeof(names) / sizeof(names[0])); f++)
    {
        for (c = 0U; c < (sizeof(chunks) / sizeof(chunks[0])); c++)
        {
            test_blob_t blob;
            test_pcm_t ref;
            test_feeder_t feeder;
            test_facts_t facts;

            test_load_fixture(names[f], &blob);
            test_decode_reference(blob.Data, blob.Size, (uint32_t)blob.Size, &ref);
            assert((ref.Frames % HALF_FRAMES) == 0U);
            test_begin_stream(&blob, blob.Size, chunks[c], &feeder);
            memset(&facts, 0, sizeof(facts));

            assert(test_play_until_ended(&feeder, &facts, 1000U));

            assert(facts.Ended == 1U);
            assert(facts.Started == 1U);
            assert(facts.FirstFrames == 1U);
            assert(facts.TimeWentBackwards == 0U);
            assert(g_fake_audio.StartCount == 1U);
            assert(g_fake_audio.StopCount == 1U);
            assert(g_fake_audio.Calls[g_fake_audio.CallCount - 1U].Kind == FAKE_AUDIO_CALL_STOP);
            assert(g_fake_audio.OwnershipViolations == 0U);
            assert(g_fake_audio.ContractViolations == 0U);
            test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
            /* 数据半区共 N 个：预填 2 个，其余每个对应一次提交；再多一次提交的是流尾静音半区。 */
            assert(g_fake_audio.SubmitCount == (ref.Frames / HALF_FRAMES) - 1U);

            test_free_pcm(&ref);
            test_free_blob(&blob);
        }
    }
}

/**
 * @brief 流尾放完之前，紧跟最后一个数据半区的那个半区已被填成静音并提交。
 * @note  在最后一个数据半区被释放之前检查：被提交的最后一个半区内容全 0。
 */
static void test_silence_half_follows_last_data_half_before_stop(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;
    const uint32_t data_halves = 30U;
    uint32_t i;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    test_decode_reference(blob.Data, blob.Size, (uint32_t)blob.Size, &ref);
    assert(ref.Frames == (size_t)data_halves * HALF_FRAMES);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);

    /* 释放到只剩最后一个数据半区在播：第 data_halves-1 次释放后补的是静音。 */
    for (i = 0U; i < (data_halves - 1U); i++)
    {
        assert(fake_audio_release_half() == 1);
        assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
        assert(!report.StreamEnded);
    }
    {
        const uint32_t silent_half = (data_halves - 1U - 1U) & 1U; /* 第 (data_halves-1) 次释放的半区 */
        const int16_t *clean = fake_audio_clean_half(silent_half);
        size_t s;

        assert(g_fake_audio.Calls[g_fake_audio.CallCount - 1U].Kind == FAKE_AUDIO_CALL_SUBMIT);
        assert(g_fake_audio.Calls[g_fake_audio.CallCount - 1U].Arg == silent_half);
        for (s = 0U; s < (size_t)HALF_FRAMES * 2U; s++)
        {
            assert(clean[s] == 0);
        }
    }
    assert(g_fake_audio.StopCount == 0U);

    assert(fake_audio_release_half() == 1);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StreamEnded);
    assert(g_fake_audio.StopCount == 1U);

    test_free_pcm(&ref);
    test_free_blob(&blob);
}

/**
 * @brief 短文件：只有一帧，预填时第二个半区直接是静音，照样 Start，放完第一个半区后回报流尾放完。
 * @note  按音频帧边界截断 CBR 夹具得到短文件；数据不足两个半区，不等更多输入。
 */
static void test_short_file_starts_with_silence_half_and_ends_after_data_half(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    size_t one_frame;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    one_frame = test_frame_end_offset(blob.Data, 0U, 1U);
    test_decode_reference(blob.Data, one_frame, (uint32_t)one_frame, &ref);
    assert(ref.Frames == HALF_FRAMES);
    test_begin_stream(&blob, one_frame, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));

    assert(test_play_until_ended(&feeder, &facts, 100U));

    assert(facts.Started == 1U);
    assert(facts.NeedInputs == 0U);
    assert(g_fake_audio.StartCount == 1U);
    assert(g_fake_audio.StopCount == 1U);
    assert(g_fake_audio.SubmitCount == 0U);
    test_assert_played_equals_reference_then_silence(&ref, ref.Frames);

    test_free_pcm(&ref);
    test_free_blob(&blob);
}

/**
 * @brief 非整半区的流尾：最后一个数据半区不满的部分补 0，随后是静音半区，放完后才回报流尾。
 * @note  MPEG-2 单声道夹具 41 帧 × 576 = 23616 帧 = 20.5 个半区；它的 22.05 kHz 真实 Platform 不接受，
 *        这里用 fake_audio_accept_rate_for_test() 只为构造该流尾，生产合同不变。
 */
static void test_partial_last_half_is_zero_padded_and_then_silence(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;

    test_load_fixture("mpeg2_22050_mono.mp3", &blob);
    test_decode_reference(blob.Data, blob.Size, (uint32_t)blob.Size, &ref);
    assert((ref.Frames % HALF_FRAMES) == 576U);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    fake_audio_accept_rate_for_test(22050U);
    memset(&facts, 0, sizeof(facts));

    assert(test_play_until_ended(&feeder, &facts, 1000U));

    assert(facts.Ended == 1U);
    assert(g_fake_audio.StartRate == 22050U);
    assert(g_fake_audio.StopCount == 1U);
    test_assert_played_equals_reference_then_silence(&ref, ((ref.Frames / HALF_FRAMES) + 1U) * HALF_FRAMES);

    test_free_pcm(&ref);
    test_free_blob(&blob);
}

/**
 * @brief 已解码时间在整个输出过程中只增不减，终值等于参考 PCM 的时长。
 */
static void test_decoded_time_never_goes_backwards_while_playing(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;

    test_load_fixture("vbr_xing_44100_stereo.mp3", &blob);
    test_decode_reference(blob.Data, blob.Size, (uint32_t)blob.Size, &ref);
    test_begin_stream(&blob, blob.Size, 313U, &feeder);
    memset(&facts, 0, sizeof(facts));

    assert(test_play_until_ended(&feeder, &facts, 1000U));
    assert(facts.TimeWentBackwards == 0U);
    assert(facts.LastDecodedMs == (uint32_t)((ref.Frames * 1000U) / 44100U));

    test_free_pcm(&ref);
    test_free_blob(&blob);
}

/**
 * @brief 测试入口。
 * @return 0 表示全部通过；断言失败会直接终止进程。
 */
int main(void)
{
    test_first_pump_prefills_two_halves_and_starts_output();
    test_prefill_waits_for_input_then_starts_without_losing_audio();
    test_released_half_is_refilled_with_next_audio_and_submitted();
    test_every_44k_fixture_plays_through_in_order_and_reports_stream_end();
    test_silence_half_follows_last_data_half_before_stop();
    test_short_file_starts_with_silence_half_and_ends_after_data_half();
    test_partial_last_half_is_zero_padded_and_then_silence();
    test_decoded_time_never_goes_backwards_while_playing();
    (void)printf("playback_service_output: all tests passed\n");
    return 0;
}
