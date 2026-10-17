/**
 * @file test_playback_service_info.c
 * @brief 播放模块首帧信息、三种总时长来源与已解码时间。
 * @note  真实 minimp3 + 小 MP3 夹具；期望值由夹具的帧数与字节数推出，见 fixtures/README.md。
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Tests/playback_service/playback_service_test_support.h"

/** @brief 帧数 × 每帧采样数 → 毫秒（向下取整），与产品口径一致。 */
static uint32_t frames_to_ms(uint32_t frames, uint32_t samples_per_frame, uint32_t hz)
{
    return (uint32_t)(((uint64_t)frames * samples_per_frame * 1000ULL) / hz);
}

/**
 * @brief 四个夹具的首帧信息：采样率、声道数，且只回报一次，落在带 PCM 的那次推进上。
 * @note  Xing 与 VBRI 元数据帧本身不出 PCM，首帧信息在其后的第一个音频帧上回报。
 */
static void test_first_frame_info_per_fixture(void)
{
    static const struct
    {
        const char *Name;
        uint32_t Hz;
        uint8_t Channels;
        uint32_t SkippedBeforeFirst;
    } cases[] = {
        {"cbr_44100_stereo.mp3", 44100U, 2U, 0U},
        {"vbr_xing_44100_stereo.mp3", 44100U, 2U, 1U},
        {"vbri_44100_stereo.mp3", 44100U, 2U, 1U},
        {"mpeg2_22050_mono.mp3", 22050U, 1U, 0U},
    };
    size_t i;

    for (i = 0U; i < (sizeof(cases) / sizeof(cases[0])); i++)
    {
        test_blob_t blob;
        test_run_t run;

        test_load_fixture(cases[i].Name, &blob);
        test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 1000U, &run);

        assert(run.FirstFrameReports == 1U);
        assert(run.FirstFrameAtResult == (uint32_t)SERVICE_PLAYBACK_ADVANCE_FRAME);
        assert(run.StreamInfo.SampleRateHz == cases[i].Hz);
        assert(run.StreamInfo.Channels == cases[i].Channels);
        assert(run.SkippedBeforeFirst == cases[i].SkippedBeforeFirst);
        test_free_blob(&blob);
    }
}

/**
 * @brief 总时长来源：Xing 与 VBRI 的帧数优先；缺失时按音频字节数估算。
 */
static void test_total_duration_from_xing_vbri_or_estimate(void)
{
    test_blob_t blob;
    test_run_t run;

    /* Xing：30 帧 × 1152 / 44100。 */
    test_load_fixture("vbr_xing_44100_stereo.mp3", &blob);
    test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 0U, &run);
    assert(run.StreamInfo.DurationFromHeader);
    assert(run.StreamInfo.TotalDurationMs == frames_to_ms(30U, 1152U, 44100U));
    test_free_blob(&blob);

    /* Xing 存在时不依赖音频字节数。 */
    test_load_fixture("vbr_xing_44100_stereo.mp3", &blob);
    test_run_stream(blob.Data, blob.Size, 0U, 0U, &run);
    assert(run.StreamInfo.DurationFromHeader);
    assert(run.StreamInfo.TotalDurationMs == frames_to_ms(30U, 1152U, 44100U));
    test_free_blob(&blob);

    /* VBRI：24 帧 × 1152 / 44100，尽管码流是 112 kbps CBR（估算会得到略不同的数）。 */
    test_load_fixture("vbri_44100_stereo.mp3", &blob);
    test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 0U, &run);
    assert(run.StreamInfo.DurationFromHeader);
    assert(run.StreamInfo.TotalDurationMs == frames_to_ms(24U, 1152U, 44100U));
    test_free_blob(&blob);

    /* 无流头的 CBR：音频字节数 × 8 / 128 kbps。 */
    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 0U, &run);
    assert(!run.StreamInfo.DurationFromHeader);
    assert(run.StreamInfo.TotalDurationMs == (uint32_t)((blob.Size * 8U) / 128U));
    assert(run.StreamInfo.TotalDurationMs == 783U); /* 与真实 30 帧时长 783 ms 相符。 */
    test_free_blob(&blob);

    /* MPEG-2 单声道估算：音频字节数 × 8 / 32 kbps；真实 41 帧 × 576 / 22050 = 1071 ms。 */
    test_load_fixture("mpeg2_22050_mono.mp3", &blob);
    test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 0U, &run);
    assert(!run.StreamInfo.DurationFromHeader);
    assert(run.StreamInfo.TotalDurationMs == (uint32_t)((blob.Size * 8U) / 32U));
    assert(run.StreamInfo.TotalDurationMs == 1071U);
    test_free_blob(&blob);

    /* 音频字节数未知且没有流头：总时长未知。 */
    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    test_run_stream(blob.Data, blob.Size, 0U, 0U, &run);
    assert(run.StreamInfo.TotalDurationMs == 0U);
    test_free_blob(&blob);
}

/**
 * @brief 已解码时间按已解出的每声道采样数与采样率累计，单调不回退，终值等于全部帧的时长。
 * @note  逐帧核对：第 k 帧之后 DecodedMs == k × 1152 × 1000 / 44100。
 */
static void test_decoded_time_accumulates_per_decoded_sample(void)
{
    test_blob_t blob;
    Service_Playback_AdvanceReportTypeDef report;
    uint32_t k = 0U;
    uint32_t last = 0U;
    uint32_t accepted = 0U;
    size_t offset = 0U;
    int16_t pcm[PLAYBACK_SERVICE_FRAME_MAX_SAMPLES * 2U];
    uint32_t frames = 0U;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_BeginStream((uint32_t)blob.Size) == SERVICE_OK);

    for (;;)
    {
        if (offset < blob.Size)
        {
            assert(Service_Playback_Feed(blob.Data + offset, (uint32_t)(blob.Size - offset), &accepted) == SERVICE_OK);
            offset += accepted;
            if (offset >= blob.Size)
            {
                assert(Service_Playback_FinishInput() == SERVICE_OK);
            }
        }
        assert(Service_Playback_Advance(&report) == SERVICE_OK);
        assert(report.DecodedMs >= last);
        last = report.DecodedMs;
        if (report.Result == SERVICE_PLAYBACK_ADVANCE_END)
        {
            break;
        }
        if (report.Result == SERVICE_PLAYBACK_ADVANCE_FRAME)
        {
            k++;
            assert(report.DecodedMs == frames_to_ms(k, 1152U, 44100U));
            assert(Service_Playback_ReadFrame(pcm, PLAYBACK_SERVICE_FRAME_MAX_SAMPLES, &frames) == SERVICE_OK);
        }
    }

    assert(k == 30U);
    assert(last == frames_to_ms(30U, 1152U, 44100U));
    test_free_blob(&blob);
}

/**
 * @brief 解码越过总时长时，已解码时间钳在总时长。
 * @note  把 Xing 帧数改成 10 的副本：流里实际有 30 帧。
 */
static void test_decoded_time_is_clamped_to_total_duration(void)
{
    test_blob_t blob;
    test_run_t run;
    const uint32_t declared = 10U;
    size_t tag_offset = 36U + 8U; /* 帧头 4 + 边信息 32 + "Xing" 与标志 8 → 帧数字段 */

    test_load_fixture("vbr_xing_44100_stereo.mp3", &blob);
    assert(memcmp(blob.Data + 36U, "Xing", 4U) == 0);
    blob.Data[tag_offset + 0U] = 0U;
    blob.Data[tag_offset + 1U] = 0U;
    blob.Data[tag_offset + 2U] = 0U;
    blob.Data[tag_offset + 3U] = (uint8_t)declared;

    test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 0U, &run);

    assert(run.StreamInfo.TotalDurationMs == frames_to_ms(declared, 1152U, 44100U));
    assert(run.FrameResults == 30U);                              /* 声音照常全部解出 */
    assert(run.MaxDecodedMs == run.StreamInfo.TotalDurationMs);   /* 位置不越过总时长 */
    assert(run.LastDecodedMs == run.StreamInfo.TotalDurationMs);
    assert(run.TimeWentBackwards == 0);
    test_free_blob(&blob);
}

/**
 * @brief 重新开始同一首：已解码时间、首帧信息与总时长全部从头计。
 */
static void test_begin_stream_restarts_time_and_first_frame(void)
{
    test_blob_t blob;
    test_run_t first;
    test_run_t second;

    test_load_fixture("vbri_44100_stereo.mp3", &blob);
    test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 700U, &first);
    test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 700U, &second);

    assert(second.FirstFrameReports == 1U);
    assert(second.StreamInfo.TotalDurationMs == first.StreamInfo.TotalDurationMs);
    assert(second.LastDecodedMs == first.LastDecodedMs);
    assert(second.PcmHash == first.PcmHash);
    test_free_blob(&blob);
}

int main(void)
{
    test_first_frame_info_per_fixture();
    test_total_duration_from_xing_vbri_or_estimate();
    test_decoded_time_accumulates_per_decoded_sample();
    test_decoded_time_is_clamped_to_total_duration();
    test_begin_stream_restarts_time_and_first_frame();
    (void)puts("playback_service_info: OK");
    return 0;
}
