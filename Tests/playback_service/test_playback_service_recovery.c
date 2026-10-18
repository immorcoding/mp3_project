/**
 * @file test_playback_service_recovery.c
 * @brief 播放模块输出管线的恢复与故障：欠载恢复、暂停与同曲恢复的半区重放与交换、放弃当前流、
 *        Start 拒绝、传输错误与 STOP_FAILED 之后不再写半区。
 * @note  真实 minimp3 + 小 MP3 夹具 + 假 Platform 音频；半区的写入只经 Start 快照、SubmitHalf 副本与
 *        播放日志观察。期望 PCM 来自解码半边的 Advance/ReadFrame。CBR 夹具共 30 个整半区，第 j 个半区
 *        （从 0 起）就是参考 PCM 的 [j * 1152, (j + 1) * 1152) 帧。
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Tests/playback_service/fake_platform_audio.h"
#include "Tests/playback_service/playback_service_test_support.h"

#define HALF_FRAMES  (PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF)
#define HALF_SAMPLES (HALF_FRAMES * 2U)

/** @brief 参考 PCM 的第 j 个半区。 */
static const int16_t *ref_half(const test_pcm_t *ref, uint32_t j)
{
    return ref->Pcm + ((size_t)j * HALF_SAMPLES);
}

/** @brief 断言 Start 时两个半区依次是参考 PCM 的第 first、first + 1 个半区。 */
static void assert_start_halves(const test_pcm_t *ref, uint32_t first)
{
    assert(memcmp(g_fake_audio.StartSnapshot, ref_half(ref, first), HALF_SAMPLES * sizeof(int16_t)) == 0);
    assert(memcmp(g_fake_audio.StartSnapshot + HALF_SAMPLES, ref_half(ref, first + 1U), HALF_SAMPLES * sizeof(int16_t)) == 0);
}

/** @brief 往 out 里写 count 个确定性伪随机字节。 */
static void fill_garbage(uint8_t *out, size_t count, uint32_t seed)
{
    size_t i;

    for (i = 0U; i < count; i++)
    {
        seed = (seed * 1664525U) + 1013904223U;
        out[i] = (uint8_t)(seed >> 24);
    }
}

/**
 * @brief 载入 CBR 夹具、取参考 PCM、开始新流并完成预填 Start（两个半区 j0、j1 在播）。
 * @param[out] blob   夹具内容。
 * @param[out] ref    参考 PCM。
 * @param[out] feeder 喂入器。
 * @param[out] facts  累计事实（已清零并含第一次推进）。
 */
static void start_cbr_playing(test_blob_t *blob, test_pcm_t *ref, test_feeder_t *feeder, test_facts_t *facts)
{
    Service_Playback_PumpReportTypeDef report;

    test_load_fixture("cbr_44100_stereo.mp3", blob);
    test_decode_reference(blob->Data, blob->Size, (uint32_t)blob->Size, ref);
    assert(ref->Frames == (size_t)30U * HALF_FRAMES);
    test_begin_stream(blob, blob->Size, 0U, feeder);
    memset(facts, 0, sizeof(*facts));
    assert(test_pump(feeder, facts, &report) == SERVICE_OK);
    assert(report.OutputStarted);
}

/** @brief 释放一个半区并 Pump 一次。 */
static void release_and_pump(test_feeder_t *feeder, test_facts_t *facts, Service_Playback_PumpReportTypeDef *report)
{
    assert(fake_audio_release_half() == 1);
    assert(test_pump(feeder, facts, report) == SERVICE_OK);
}

/** @brief 释放结束并清理。 */
static void finish(test_blob_t *blob, test_pcm_t *ref)
{
    test_free_pcm(ref);
    test_free_blob(blob);
}

/**
 * @brief 欠载（半区释放时另一半区未提交）：Stop → 补满两个半区 → Start，内容接着解码进度，
 *        不重复也不丢，对外只多一个欠载次数。
 * @note  主动让两次释放连续发生而不 Pump 来制造欠载；其后放到流尾，播放日志应与参考 PCM 完全一致。
 */
static void test_underrun_stops_refills_and_restarts_without_repeating_audio(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    assert(fake_audio_release_half() == 1);
    assert(fake_audio_release_half() == 1);

    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);

    assert(report.Underruns == 1U);
    assert(report.OutputStarted);
    assert(!report.StreamEnded && !report.NeedInput);
    assert(g_fake_audio.StopCount == 1U);
    assert(g_fake_audio.StartCount == 2U);
    assert(g_fake_audio.SubmitCount == 0U);
    assert_start_halves(&ref, 2U);

    assert(test_play_until_ended(&feeder, &facts, 1000U));
    assert(facts.TimeWentBackwards == 0U);
    assert(facts.Underruns == 1U);
    assert(g_fake_audio.OwnershipViolations == 0U);
    assert(g_fake_audio.ContractViolations == 0U);
    test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
    finish(&blob, &ref);
}

/**
 * @brief 欠载恢复时输入不够：已 Stop，不 Start，回报需要输入；补足输入后下一次推进才 Start，内容仍接上。
 * @note  只给 3000 B：预填后窗口低于 W_min，补半区时需要更多输入。
 */
static void test_underrun_recovery_waits_for_input_before_restart(void)
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
    feeder.Limit = 3000U;
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.OutputStarted);

    assert(fake_audio_release_half() == 1);
    assert(fake_audio_release_half() == 1);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.Underruns == 1U);
    assert(report.NeedInput);
    assert(!report.OutputStarted);
    assert(g_fake_audio.StopCount == 1U);
    assert(g_fake_audio.StartCount == 1U);

    feeder.Limit = 0U;
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.OutputStarted);
    assert(g_fake_audio.StartCount == 2U);
    assert_start_halves(&ref, 2U);

    assert(test_play_until_ended(&feeder, &facts, 1000U));
    test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
    finish(&blob, &ref);
}

/**
 * @brief 暂停 = Stop；暂停期间不 Start、不提交、不动半区；恢复 = Start，且已解码时间不回退。
 * @note  暂停时 DMA 在读半区 0（尚无释放），恢复后两个半区原样、半区 0 在前。
 */
static void test_pause_stops_output_and_resume_replays_from_the_same_halves(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;
    uint32_t i;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    assert(Service_Playback_PauseOutput() == SERVICE_OK);
    assert(Service_Playback_PauseOutput() == SERVICE_OK);
    assert(g_fake_audio.StopCount == 1U);
    assert(!g_fake_audio.Running);

    for (i = 0U; i < 5U; i++)
    {
        assert(fake_audio_release_half() == 0);
        assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
        assert(!report.OutputStarted && !report.OutputProgress && !report.NeedInput);
    }
    assert(g_fake_audio.StartCount == 1U);
    assert(g_fake_audio.SubmitCount == 0U);
    assert(report.DecodedMs == facts.LastDecodedMs);

    assert(Service_Playback_ResumeOutput() == SERVICE_OK);
    assert(Service_Playback_ResumeOutput() == SERVICE_OK);
    assert(g_fake_audio.StartCount == 2U);
    assert_start_halves(&ref, 0U);

    assert(test_play_until_ended(&feeder, &facts, 1000U));
    assert(facts.TimeWentBackwards == 0U);
    test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
    finish(&blob, &ref);
}

/**
 * @brief 同曲恢复：暂停时 DMA 正在读半区 1，恢复要从半区 1 的开头重放，另一半区接在后面；
 *        Start 总从半区 0 开始，所以两个半区内容被交换。
 * @note  放到第 1 次释放（半区 0 被读完并补成 j2）后暂停：DMA 在读 j1（半区 1）。期望 Start 时半区 0 = j1，
 *        半区 1 = j2。重放的是已部分播出的半区，播放日志里该半区只出现一次完整的。
 */
static void test_resume_replays_the_half_dma_was_reading_by_swapping_halves(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    release_and_pump(&feeder, &facts, &report);
    assert(g_fake_audio.SubmitCount == 1U);

    assert(Service_Playback_PauseOutput() == SERVICE_OK);
    assert(Service_Playback_ResumeOutput() == SERVICE_OK);

    assert(g_fake_audio.StartCount == 2U);
    assert_start_halves(&ref, 1U);
    assert(g_fake_audio.SubmitCount == 1U);

    assert(test_play_until_ended(&feeder, &facts, 1000U));
    assert(facts.TimeWentBackwards == 0U);
    assert(g_fake_audio.OwnershipViolations == 0U);
    test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
    finish(&blob, &ref);
}

/**
 * @brief 暂停时已有一个半区被读完但还没补：暂停期间的推进把它补满（不提交、不 Start），恢复后顺序正确。
 * @note  两个变体：暂停期间有推进补满；暂停期间没有推进，恢复时才补满。结果应相同。
 */
static void test_pause_with_a_released_half_still_to_refill_resumes_in_order(void)
{
    uint32_t pump_while_paused;

    for (pump_while_paused = 0U; pump_while_paused < 2U; pump_while_paused++)
    {
        test_blob_t blob;
        test_pcm_t ref;
        test_feeder_t feeder;
        test_facts_t facts;
        Service_Playback_PumpReportTypeDef report;

        start_cbr_playing(&blob, &ref, &feeder, &facts);
        assert(fake_audio_release_half() == 1);
        assert(Service_Playback_PauseOutput() == SERVICE_OK);

        if (pump_while_paused != 0U)
        {
            assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
            assert(!report.OutputStarted);
            assert(g_fake_audio.SubmitCount == 0U);
            assert(g_fake_audio.StartCount == 1U);
        }

        assert(Service_Playback_ResumeOutput() == SERVICE_OK);
        assert(g_fake_audio.StartCount == 2U);
        assert(g_fake_audio.SubmitCount == 0U);
        assert_start_halves(&ref, 1U);

        assert(test_play_until_ended(&feeder, &facts, 1000U));
        test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
        finish(&blob, &ref);
    }
}

/**
 * @brief 暂停时两个半区都已被读完（欠载在先），甚至多读了一次：恢复时按解码进度重新补满，Start 的两个半区
 *        是紧接着解码进度的下两段，不重复也不错序。
 * @note  释放 2 次时按序补满即可；释放 3 次时按序补满会覆盖尚未重放的半区，必须整体重来。
 */
static void test_pause_after_both_halves_were_released_refills_from_decode_progress(void)
{
    uint32_t releases;

    for (releases = 2U; releases <= 3U; releases++)
    {
        test_blob_t blob;
        test_pcm_t ref;
        test_feeder_t feeder;
        test_facts_t facts;
        uint32_t i;

        start_cbr_playing(&blob, &ref, &feeder, &facts);
        for (i = 0U; i < releases; i++)
        {
            assert(fake_audio_release_half() == 1);
        }
        assert(Service_Playback_PauseOutput() == SERVICE_OK);
        assert(Service_Playback_ResumeOutput() == SERVICE_OK);

        assert(g_fake_audio.StartCount == 2U);
        assert_start_halves(&ref, 2U);
        assert(test_play_until_ended(&feeder, &facts, 1000U));
        test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
        finish(&blob, &ref);
    }
}

/**
 * @brief 暂停中换曲的预填：BeginStream 后先暂停，推进只解首帧并补满两个半区，不 Start，也给出首帧信息；
 *        恢复后才出声。
 */
static void test_prefill_while_paused_decodes_first_frame_without_starting(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    test_load_fixture("vbr_xing_44100_stereo.mp3", &blob);
    test_decode_reference(blob.Data, blob.Size, (uint32_t)blob.Size, &ref);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));

    assert(Service_Playback_PauseOutput() == SERVICE_OK);
    assert(g_fake_audio.StopCount == 0U);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);

    assert(report.FirstFrame);
    assert(report.StreamInfo.SampleRateHz == 44100U);
    assert(report.StreamInfo.TotalDurationMs > 0U);
    assert(report.DecodedMs == (2U * HALF_FRAMES * 1000U) / 44100U);
    assert(!report.OutputStarted);
    assert(g_fake_audio.StartCount == 0U);

    assert(Service_Playback_ResumeOutput() == SERVICE_OK);
    assert(g_fake_audio.StartCount == 1U);
    assert_start_halves(&ref, 0U);
    assert(test_play_until_ended(&feeder, &facts, 1000U));
    test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
    finish(&blob, &ref);
}

/**
 * @brief 排空期间暂停：恢复后继续排空，仍放完整条流并回报流尾放完。
 * @note  放到最后一个数据半区在播、其后静音半区已提交时暂停；DMA 正在读奇数半区，恢复会交换半区，
 *        最后的数据半区的“最后”标记要跟着内容走。
 */
static void test_pause_during_drain_resumes_and_still_ends_after_last_data(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;
    uint32_t i;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    for (i = 0U; i < 29U; i++)
    {
        release_and_pump(&feeder, &facts, &report);
        assert(!report.StreamEnded);
    }

    assert(Service_Playback_PauseOutput() == SERVICE_OK);
    assert(Service_Playback_ResumeOutput() == SERVICE_OK);
    assert(g_fake_audio.Running);
    assert(memcmp(g_fake_audio.StartSnapshot, ref_half(&ref, 29U), HALF_SAMPLES * sizeof(int16_t)) == 0);

    release_and_pump(&feeder, &facts, &report);
    assert(report.StreamEnded);
    assert(g_fake_audio.StopCount == 2U);
    test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
    finish(&blob, &ref);
}

/**
 * @brief 最后一个数据半区在暂停之前已被读完：暂停后推进回报流尾放完，恢复不再出声。
 */
static void test_pause_after_last_data_half_was_released_reports_stream_end(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;
    uint32_t i;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    for (i = 0U; i < 29U; i++)
    {
        release_and_pump(&feeder, &facts, &report);
    }
    assert(fake_audio_release_half() == 1);
    assert(Service_Playback_PauseOutput() == SERVICE_OK);

    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StreamEnded);
    assert(Service_Playback_ResumeOutput() == SERVICE_OK);
    assert(g_fake_audio.StartCount == 1U);
    test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
    finish(&blob, &ref);
}

/**
 * @brief Start 因参数被拒（Platform 报错但未锁存）：回报 StartRejected 事实，不是 STOP_FAILED，之后不写半区。
 * @note  两种触发：假 Platform 注入拒绝；MPEG-2 夹具的 22.05 kHz 不在 Platform 支持的采样率里（B6 跳过）。
 */
static void test_start_rejected_by_parameter_is_reported_and_not_latched(void)
{
    test_blob_t blob;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));
    fake_audio_reject_next_start();
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StartRejected);
    assert(!report.StopFailed && !report.OutputStarted);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StartRejected);
    assert(g_fake_audio.StartCount == 0U);
    assert(g_fake_audio.SubmitCount == 0U);
    assert(Service_Playback_AbandonStream() == SERVICE_OK);
    test_free_blob(&blob);

    test_load_fixture("mpeg2_22050_mono.mp3", &blob);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StartRejected);
    assert(!report.StopFailed);
    assert(report.FirstFrame);
    assert(report.StreamInfo.SampleRateHz == 22050U);
    assert(g_fake_audio.StartCount == 0U);

    /* 新流开始后拒绝事实随旧流清除。 */
    test_free_blob(&blob);
    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    assert(Service_Playback_BeginStream((uint32_t)blob.Size) == SERVICE_OK);
    test_feeder_init(&feeder, blob.Data, blob.Size, 0U);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.OutputStarted && !report.StartRejected);
    test_free_blob(&blob);
}

/**
 * @brief Start 被拒且 Platform 已锁存 STOP_FAILED：回报 StopFailed，而不是参数拒绝；之后不 Start、不提交。
 */
static void test_start_rejected_by_latched_fault_is_reported_as_stop_failed(void)
{
    test_blob_t blob;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));
    fake_audio_latch_stop_failed();

    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StopFailed);
    assert(!report.StartRejected);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StopFailed);
    assert(g_fake_audio.StartCount == 0U);
    assert(g_fake_audio.SubmitCount == 0U);
    test_free_blob(&blob);
}

/**
 * @brief TRANSFER_ERROR：回报事实并持续给出，之后不再补半区、不提交；放弃当前流时 Stop，之后可开始新流。
 */
static void test_transfer_error_is_reported_without_further_writes_and_abandon_stops(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    fake_audio_inject_transfer_error();
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.TransferError);
    assert(!report.StopFailed);

    assert(fake_audio_release_half() == 1);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.TransferError);
    assert(g_fake_audio.SubmitCount == 0U);
    assert(g_fake_audio.StopCount == 0U);

    assert(Service_Playback_AbandonStream() == SERVICE_OK);
    assert(g_fake_audio.StopCount == 1U);
    assert(Service_Playback_Pump(&report) == SERVICE_NOT_READY);

    assert(Service_Playback_BeginStream((uint32_t)blob.Size) == SERVICE_OK);
    test_feeder_init(&feeder, blob.Data, blob.Size, 0U);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.OutputStarted && !report.TransferError);
    assert_start_halves(&ref, 0U);
    finish(&blob, &ref);
}

/** @brief 记录两个半区当前的样本，用来证明之后没有被写。 */
typedef struct
{
    int16_t Samples[2U * HALF_SAMPLES];
} ring_copy_t;

/** @brief 取走 Start 时的环形缓冲当前内容。 */
static void snapshot_ring(ring_copy_t *copy)
{
    memcpy(copy->Samples, g_fake_audio.StartRing, sizeof(copy->Samples));
}

/** @brief 断言环形缓冲与快照一致。 */
static void assert_ring_unchanged(const ring_copy_t *copy)
{
    assert(memcmp(copy->Samples, g_fake_audio.StartRing, sizeof(copy->Samples)) == 0);
}

/**
 * @brief 暂停时 Stop 失败：返回错误，之后推进回报 StopFailed，不再写半区、不提交、不再 Start，
 *        恢复、开始新流都被拒。
 */
static void test_stop_failed_on_pause_stops_all_writes(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;
    ring_copy_t before;
    uint32_t i;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    fake_audio_fail_next_stop();
    assert(Service_Playback_PauseOutput() == SERVICE_ERROR);
    snapshot_ring(&before);

    for (i = 0U; i < 4U; i++)
    {
        (void)fake_audio_release_half();
        assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
        assert(report.StopFailed);
        assert(!report.StreamEnded && !report.OutputStarted);
    }
    assert(g_fake_audio.SubmitCount == 0U);
    assert(g_fake_audio.StartCount == 1U);
    assert_ring_unchanged(&before);
    assert(Service_Playback_ResumeOutput() == SERVICE_ERROR);
    assert(Service_Playback_PauseOutput() == SERVICE_ERROR);
    assert(Service_Playback_BeginStream(1000U) == SERVICE_NOT_READY);
    assert(g_fake_audio.StartCount == 1U);
    finish(&blob, &ref);
}

/**
 * @brief 暂停时 Stop 成功而读状态失败：不算 Stop 失败，返回 SERVICE_OK、不锁存 STOP_FAILED，仍处于暂停，
 *        恢复后按最近一次推进记下的计数重放并播完全程。
 * @note  先释放一个半区并 Pump 一次，让“最近已知”的释放数为 1；GetStatus 失败那次暂停沿用它。
 */
static void test_get_status_failure_after_successful_stop_is_not_a_stop_failure(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    release_and_pump(&feeder, &facts, &report);

    fake_audio_fail_next_get_status();
    assert(Service_Playback_PauseOutput() == SERVICE_OK);
    assert(g_fake_audio.StopCount == 1U);
    assert(!g_fake_audio.Running);
    assert(Service_Playback_PauseOutput() == SERVICE_OK);
    assert(g_fake_audio.StopCount == 1U);

    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(!report.StopFailed && !report.OutputStarted);

    assert(Service_Playback_ResumeOutput() == SERVICE_OK);
    assert(g_fake_audio.StartCount == 2U);
    assert_start_halves(&ref, 1U);
    assert(test_play_until_ended(&feeder, &facts, 1000U));
    assert(facts.StopFailed == 0U);
    test_assert_played_equals_reference_then_silence(&ref, ref.Frames);
    finish(&blob, &ref);
}

/**
 * @brief 欠载恢复的 Stop 失败：回报 StopFailed，不再补满、不再 Start。
 */
static void test_stop_failed_during_underrun_recovery_stops_all_writes(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;
    ring_copy_t before;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    snapshot_ring(&before);
    assert(fake_audio_release_half() == 1);
    assert(fake_audio_release_half() == 1);
    fake_audio_fail_next_stop();

    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StopFailed);
    assert(!report.OutputStarted);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StopFailed);
    assert(g_fake_audio.StartCount == 1U);
    assert(g_fake_audio.SubmitCount == 0U);
    assert_ring_unchanged(&before);
    finish(&blob, &ref);
}

/**
 * @brief 流尾放完时的 Stop 失败：回报 StopFailed 而不是流尾放完，之后不再写半区。
 */
static void test_stop_failed_at_stream_end_is_reported_instead_of_stream_end(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;
    ring_copy_t before;
    uint32_t i;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    for (i = 0U; i < 29U; i++)
    {
        release_and_pump(&feeder, &facts, &report);
    }
    snapshot_ring(&before);
    fake_audio_fail_next_stop();
    release_and_pump(&feeder, &facts, &report);

    assert(report.StopFailed);
    assert(!report.StreamEnded);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.StopFailed && !report.StreamEnded);
    assert_ring_unchanged(&before);
    finish(&blob, &ref);
}

/**
 * @brief 放弃当前流时 Stop 失败：返回错误并锁存，之后不能再开始新流。
 */
static void test_stop_failed_on_abandon_blocks_new_streams(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    fake_audio_fail_next_stop();
    assert(Service_Playback_AbandonStream() == SERVICE_ERROR);
    assert(Service_Playback_BeginStream(1000U) == SERVICE_NOT_READY);
    assert(g_fake_audio.StartCount == 1U);

    /* 只有重新 Init（重启）才解除记忆；假 Platform 也要同时复位。 */
    fake_audio_reset();
    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_BeginStream(1000U) == SERVICE_OK);
    finish(&blob, &ref);
}

/**
 * @brief 放弃当前流：Stop 输出、流失效；重复放弃无副作用；之后新流从头开始，不混入旧流的半区。
 */
static void test_abandon_stops_output_and_a_new_stream_starts_from_its_beginning(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    release_and_pump(&feeder, &facts, &report);

    assert(Service_Playback_AbandonStream() == SERVICE_OK);
    assert(Service_Playback_AbandonStream() == SERVICE_OK);
    assert(g_fake_audio.StopCount == 1U);
    assert(Service_Playback_Pump(&report) == SERVICE_NOT_READY);
    assert(Service_Playback_PauseOutput() == SERVICE_NOT_READY);
    assert(Service_Playback_ResumeOutput() == SERVICE_NOT_READY);

    assert(Service_Playback_BeginStream((uint32_t)blob.Size) == SERVICE_OK);
    test_feeder_init(&feeder, blob.Data, blob.Size, 0U);
    memset(&facts, 0, sizeof(facts));
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.OutputStarted && report.FirstFrame);
    assert(report.DecodedMs == (2U * HALF_FRAMES * 1000U) / 44100U);
    assert(g_fake_audio.StartCount == 2U);
    assert_start_halves(&ref, 0U);
    finish(&blob, &ref);
}

/**
 * @brief 输出仍在运行时开始新流：先 Stop 旧输出，再重置。
 */
static void test_begin_stream_while_running_stops_the_old_output_first(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    assert(Service_Playback_BeginStream((uint32_t)blob.Size) == SERVICE_OK);
    assert(g_fake_audio.StopCount == 1U);
    assert(!g_fake_audio.Running);
    finish(&blob, &ref);
}

/**
 * @brief 垃圾字节：同步预算耗尽回报 SyncExhausted，没有首帧、不 Start；输入读尽而一帧都没有同样不可播。
 */
static void test_unplayable_input_reports_sync_exhausted_without_starting(void)
{
    const size_t size = PLAYBACK_SERVICE_SYNC_BUDGET_BYTES + 6000U;
    uint8_t *garbage = (uint8_t *)malloc(size);
    test_blob_t blob;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;
    uint32_t rounds;

    assert(garbage != NULL);
    fill_garbage(garbage, size, 12345U);
    blob.Data = garbage;
    blob.Size = size;
    test_begin_stream(&blob, size, 1000U, &feeder);
    memset(&facts, 0, sizeof(facts));
    for (rounds = 0U; rounds < 500U; rounds++)
    {
        assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
        if (report.SyncExhausted)
        {
            break;
        }
    }
    assert(report.SyncExhausted);
    assert(!report.FirstFrame && !report.OutputStarted);
    assert(g_fake_audio.StartCount == 0U);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.SyncExhausted);

    /* 少于 R 的短垃圾读尽：没有任何帧，同样不可播。 */
    blob.Size = 100U;
    test_begin_stream(&blob, 100U, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(report.SyncExhausted);
    assert(g_fake_audio.StartCount == 0U);
    free(garbage);
}

/**
 * @brief 一条流由 Pump 驱动后，解码半边的 Advance/ReadFrame 被拒（BUSY），开始新流后恢复可用。
 */
static void test_decode_half_cannot_be_mixed_with_pump_on_one_stream(void)
{
    test_blob_t blob;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;
    Service_Playback_AdvanceReportTypeDef advance;
    int16_t stereo[PLAYBACK_SERVICE_FRAME_MAX_SAMPLES * 2U];
    uint32_t frames = 0U;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);

    assert(Service_Playback_Advance(&advance) == SERVICE_BUSY);
    assert(Service_Playback_ReadFrame(stereo, PLAYBACK_SERVICE_FRAME_MAX_SAMPLES, &frames) == SERVICE_BUSY);

    assert(Service_Playback_BeginStream((uint32_t)blob.Size) == SERVICE_OK);
    assert(Service_Playback_Advance(&advance) == SERVICE_OK);
    test_free_blob(&blob);
}

/**
 * @brief 输出有进展：每次释放之后的推进回报 OutputProgress，没有新释放时不报；Start 之后重新起算。
 */
static void test_output_progress_is_reported_per_new_release(void)
{
    test_blob_t blob;
    test_pcm_t ref;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_PumpReportTypeDef report;

    start_cbr_playing(&blob, &ref, &feeder, &facts);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(!report.OutputProgress);
    release_and_pump(&feeder, &facts, &report);
    assert(report.OutputProgress);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(!report.OutputProgress);
    release_and_pump(&feeder, &facts, &report);
    assert(report.OutputProgress);

    assert(Service_Playback_PauseOutput() == SERVICE_OK);
    assert(Service_Playback_ResumeOutput() == SERVICE_OK);
    assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    assert(!report.OutputProgress);
    release_and_pump(&feeder, &facts, &report);
    assert(report.OutputProgress);
    finish(&blob, &ref);
}

/**
 * @brief 测试入口。
 * @return 0 表示全部通过；断言失败会直接终止进程。
 */
int main(void)
{
    test_underrun_stops_refills_and_restarts_without_repeating_audio();
    test_underrun_recovery_waits_for_input_before_restart();
    test_pause_stops_output_and_resume_replays_from_the_same_halves();
    test_resume_replays_the_half_dma_was_reading_by_swapping_halves();
    test_pause_with_a_released_half_still_to_refill_resumes_in_order();
    test_pause_after_both_halves_were_released_refills_from_decode_progress();
    test_prefill_while_paused_decodes_first_frame_without_starting();
    test_pause_during_drain_resumes_and_still_ends_after_last_data();
    test_pause_after_last_data_half_was_released_reports_stream_end();
    test_start_rejected_by_parameter_is_reported_and_not_latched();
    test_start_rejected_by_latched_fault_is_reported_as_stop_failed();
    test_transfer_error_is_reported_without_further_writes_and_abandon_stops();
    test_stop_failed_on_pause_stops_all_writes();
    test_get_status_failure_after_successful_stop_is_not_a_stop_failure();
    test_stop_failed_during_underrun_recovery_stops_all_writes();
    test_stop_failed_at_stream_end_is_reported_instead_of_stream_end();
    test_stop_failed_on_abandon_blocks_new_streams();
    test_abandon_stops_output_and_a_new_stream_starts_from_its_beginning();
    test_begin_stream_while_running_stops_the_old_output_first();
    test_unplayable_input_reports_sync_exhausted_without_starting();
    test_decode_half_cannot_be_mixed_with_pump_on_one_stream();
    test_output_progress_is_reported_per_new_release();
    (void)printf("playback_service_recovery: all tests passed\n");
    return 0;
}
