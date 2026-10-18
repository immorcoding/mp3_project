/**
 * @file test_playback_service_sync.c
 * @brief 播放模块同步预算 R 与采样率不同的帧丢弃。
 * @note  真实 minimp3 + 小 MP3 夹具；垃圾字节由固定种子的线性同余发生器现场生成，结果与平台无关。
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Tests/playback_service/playback_service_test_support.h"

#define CBR_AUDIO_FRAMES   (30U)
#define MPEG2_AUDIO_FRAMES (41U)

/**
 * @brief 把 src 追加到 dst 的 *used 之后。
 * @param[out]    dst   目标缓冲，容量须容得下追加后的内容。
 * @param[in,out] used  已用字节数，追加后增加 count。
 * @param[in]     src   源数据。
 * @param[in]     count 追加的字节数。
 */
static void append(uint8_t *dst, size_t *used, const uint8_t *src, size_t count)
{
    memcpy(dst + *used, src, count);
    *used += count;
}

/**
 * @brief 垃圾字节在累计 R 字节后回报同步预算耗尽，且没有首帧、没有 PCM；耗尽状态在新流开始前保持。
 */
static void test_garbage_exhausts_sync_budget(void)
{
    const size_t size = PLAYBACK_SERVICE_SYNC_BUDGET_BYTES + 6000U;
    uint8_t *garbage = (uint8_t *)malloc(size);
    test_run_t run;
    Service_Playback_AdvanceReportTypeDef report;

    assert(garbage != NULL);
    test_fill_garbage(garbage, size, 12345U);

    test_run_stream(garbage, size, (uint32_t)size, 1000U, &run);

    assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED);
    assert(run.FirstFrameReports == 0U);
    assert(run.FrameResults == 0U);
    assert(run.StereoFrames == 0U);

    /* 耗尽后保持，不再消耗输入。 */
    assert(Service_Playback_Advance(&report) == SERVICE_OK);
    assert(report.Result == SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED);
    assert(Service_Playback_Advance(&report) == SERVICE_OK);
    assert(report.Result == SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED);

    /* 新流清除耗尽状态。 */
    assert(Service_Playback_BeginStream(0U) == SERVICE_OK);
    assert(Service_Playback_Advance(&report) == SERVICE_OK);
    assert(report.Result == SERVICE_PLAYBACK_ADVANCE_NEED_INPUT);
    free(garbage);
}

/**
 * @brief 少于 R 的垃圾不触发耗尽；输入结束后的残余按流尾处理，以 END 收场。
 */
static void test_garbage_shorter_than_budget_is_not_exhausted(void)
{
    const size_t size = PLAYBACK_SERVICE_SYNC_BUDGET_BYTES / 2U;
    uint8_t *garbage = (uint8_t *)malloc(size);
    test_run_t run;

    assert(garbage != NULL);
    test_fill_garbage(garbage, size, 777U);

    test_run_stream(garbage, size, (uint32_t)size, 0U, &run);

    assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
    assert(run.FirstFrameReports == 0U);
    assert(run.FrameResults == 0U);
    free(garbage);
}

/**
 * @brief 真实夹具不被误判：每个夹具前面带小于 R 的垃圾头，仍解出首帧并走到 END。
 * @note  垃圾头会让第一帧可能因落在窗口边界被连带丢弃，所以只要求至多丢一帧。
 */
static void test_real_fixtures_with_leading_junk_are_not_misjudged(void)
{
    static const char *const names[] = {"cbr_44100_stereo.mp3", "vbr_xing_44100_stereo.mp3", "vbri_44100_stereo.mp3", "mpeg2_22050_mono.mp3"};
    static const uint32_t expected[] = {CBR_AUDIO_FRAMES, 30U, 24U, MPEG2_AUDIO_FRAMES};
    size_t f;

    for (f = 0U; f < (sizeof(names) / sizeof(names[0])); f++)
    {
        test_blob_t blob;
        const size_t junk = 10000U;
        uint8_t *stream;
        size_t used = 0U;
        test_run_t run;

        test_load_fixture(names[f], &blob);
        stream = (uint8_t *)malloc(junk + blob.Size);
        assert(stream != NULL);
        test_fill_garbage(stream, junk, 99U + (uint32_t)f);
        used = junk;
        append(stream, &used, blob.Data, blob.Size);

        test_run_stream(stream, used, (uint32_t)used, 1500U, &run);

        assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
        assert(run.FirstFrameReports == 1U);
        assert(run.FrameResults >= (expected[f] - 1U));
        assert(run.FrameResults <= expected[f]);
        free(stream);
        test_free_blob(&blob);
    }
}

/**
 * @brief 流中间夹一段小于 R 的垃圾后能重新同步，剩余的帧继续解出。
 */
static void test_resyncs_after_garbage_in_the_middle(void)
{
    test_blob_t blob;
    uint8_t *stream;
    size_t used = 0U;
    test_run_t run;
    size_t cut;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    cut = test_frame_end_offset(blob.Data, 0U, 14U);
    stream = (uint8_t *)malloc(blob.Size + 3000U);
    assert(stream != NULL);
    append(stream, &used, blob.Data, cut);
    test_fill_garbage(stream + used, 3000U, 4242U);
    used += 3000U;
    append(stream, &used, blob.Data + cut, blob.Size - cut);

    test_run_stream(stream, used, (uint32_t)used, 1000U, &run);

    assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
    assert(run.FirstFrameReports == 1U);
    assert(run.FrameResults >= (CBR_AUDIO_FRAMES - 4U)); /* 失步附近的帧会连带丢掉，其余全部解出。 */
    assert(run.FrameResults < CBR_AUDIO_FRAMES);
    assert(run.TimeWentBackwards == 0);
    free(stream);
    test_free_blob(&blob);
}

/**
 * @brief 采样率与首帧不同的帧被丢弃：不出 PCM，不计入已解码时间，首帧信息不变。
 * @note  44.1 kHz 立体声里夹一整段 22.05 kHz 单声道。
 */
static void test_frames_with_different_sample_rate_are_dropped(void)
{
    test_blob_t main_blob;
    test_blob_t odd_blob;
    uint8_t *stream;
    size_t used = 0U;
    size_t head;
    test_run_t run;

    test_load_fixture("cbr_44100_stereo.mp3", &main_blob);
    test_load_fixture("mpeg2_22050_mono.mp3", &odd_blob);
    head = test_frame_end_offset(main_blob.Data, 0U, 10U);
    stream = (uint8_t *)malloc(main_blob.Size + odd_blob.Size);
    assert(stream != NULL);
    append(stream, &used, main_blob.Data, head);
    append(stream, &used, odd_blob.Data, odd_blob.Size);
    append(stream, &used, main_blob.Data + head, main_blob.Size - head);

    test_run_stream(stream, used, (uint32_t)used, 1000U, &run);

    assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
    assert(run.FirstFrameReports == 1U);
    assert(run.StreamInfo.SampleRateHz == 44100U);
    assert(run.StreamInfo.Channels == 2U);
    assert(run.Frames576 == 0U);                      /* 22.05 kHz 的 576 采样帧一帧也没出。 */
    assert(run.Frames1152 == run.FrameResults);
    assert(run.FrameResults >= 10U);                  /* 插入点之前的帧都在。 */
    assert(run.FrameResults < CBR_AUDIO_FRAMES);      /* 插入点前后各有连带丢失，但不会多出来。 */
    assert(run.SkippedResults > 0U);
    assert(run.TimeWentBackwards == 0);
    assert(run.LastDecodedMs == (uint32_t)(((uint64_t)run.StereoFrames * 1000ULL) / 44100ULL));
    free(stream);
    test_free_blob(&main_blob);
    test_free_blob(&odd_blob);
}

/**
 * @brief 采样率不同的帧计入 R：首帧之后持续 R 字节以上的异频帧回报同步预算耗尽，PCM 一帧不出。
 * @note  开头放 12 帧好帧：minimp3 在没有解码器状态时要求同一格式的帧连成一串（最多 10 帧）才认帧，
 *        太短的开头会连同后面的异频帧一起被当成噪声，那样“首帧”就变成了异频帧。
 */
static void test_different_sample_rate_frames_count_against_sync_budget(void)
{
    test_blob_t main_blob;
    test_blob_t odd_blob;
    const size_t total = PLAYBACK_SERVICE_SYNC_BUDGET_BYTES + 8000U;
    uint8_t *stream;
    size_t used = 0U;
    size_t head;
    test_run_t run;

    test_load_fixture("cbr_44100_stereo.mp3", &main_blob);
    test_load_fixture("mpeg2_22050_mono.mp3", &odd_blob);
    stream = (uint8_t *)malloc(main_blob.Size + total + odd_blob.Size);
    assert(stream != NULL);
    head = test_frame_end_offset(main_blob.Data, 0U, 12U);
    append(stream, &used, main_blob.Data, head);
    while (used < (head + total))
    {
        append(stream, &used, odd_blob.Data, odd_blob.Size);
    }

    test_run_stream(stream, used, (uint32_t)used, 1000U, &run);

    assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED);
    assert(run.FirstFrameReports == 1U);
    assert(run.Frames576 == 0U);
    assert(run.FrameResults >= 10U); /* 首帧之前的好帧都在 */
    assert(run.FrameResults <= 12U);
    free(stream);
    test_free_blob(&main_blob);
    test_free_blob(&odd_blob);
}

/**
 * @brief 测试入口。
 * @return 0 表示全部通过；断言失败会直接终止进程。
 */
int main(void)
{
    test_garbage_exhausts_sync_budget();
    test_garbage_shorter_than_budget_is_not_exhausted();
    test_real_fixtures_with_leading_junk_are_not_misjudged();
    test_resyncs_after_garbage_in_the_middle();
    test_frames_with_different_sample_rate_are_dropped();
    test_different_sample_rate_frames_count_against_sync_budget();
    (void)puts("playback_service_sync: OK");
    return 0;
}
