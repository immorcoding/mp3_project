/**
 * @file playback_service_test_support.c
 * @brief 播放模块主机测试的共用夹具读取与整流驱动。
 */

#include "Tests/playback_service/playback_service_test_support.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PLAYBACK_SERVICE_FIXTURE_DIR
#error "PLAYBACK_SERVICE_FIXTURE_DIR 由 CMake 提供"
#endif

/**
 * @brief 读入 fixtures/ 下的一个夹具文件。
 * @param[in]  name 夹具文件名。
 * @param[out] blob 文件内容的堆副本；用 test_free_blob() 释放。
 * @note  找不到或读不全时直接断言失败，测试不会在缺夹具时静默通过。
 */
void test_load_fixture(const char *name, test_blob_t *blob)
{
    char path[512];
    FILE *file;
    long size;

    (void)snprintf(path, sizeof(path), "%s/%s", PLAYBACK_SERVICE_FIXTURE_DIR, name);
    file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0);
    assert(fseek(file, 0, SEEK_SET) == 0);

    blob->Size = (size_t)size;
    blob->Data = (uint8_t *)malloc(blob->Size);
    assert(blob->Data != NULL);
    assert(fread(blob->Data, 1U, blob->Size, file) == blob->Size);
    (void)fclose(file);
}

/**
 * @brief 释放 test_load_fixture() 返回的副本。
 * @param[in,out] blob 被释放并清零。
 */
void test_free_blob(test_blob_t *blob)
{
    free(blob->Data);
    blob->Data = NULL;
    blob->Size = 0U;
}

/**
 * @brief 沿 Layer III 帧头走过 count 帧。
 * @param[in] data   从帧头开始的码流。
 * @param[in] offset 起始偏移，必须在帧头上。
 * @param[in] count  要走过的帧数。
 * @return 第 count 帧末尾的字节偏移。
 */
size_t test_frame_end_offset(const uint8_t *data, size_t offset, uint32_t count)
{
    static const uint16_t bitrate_mpeg1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const uint16_t bitrate_mpeg2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const uint32_t rate_mpeg1[4] = {44100U, 48000U, 32000U, 0U};
    static const uint32_t rate_mpeg2[4] = {22050U, 24000U, 16000U, 0U};
    uint32_t i;

    for (i = 0U; i < count; i++)
    {
        const uint8_t *h = data + offset;
        const int mpeg1 = (h[1] & 0x08U) != 0U;
        const uint32_t kbps = mpeg1 ? bitrate_mpeg1[h[2] >> 4] : bitrate_mpeg2[h[2] >> 4];
        const uint32_t hz = mpeg1 ? rate_mpeg1[(h[2] >> 2) & 3U] : rate_mpeg2[(h[2] >> 2) & 3U];
        const uint32_t padding = (h[2] >> 1) & 1U;

        assert((h[0] == 0xFFU) && ((h[1] & 0xE0U) == 0xE0U));
        assert((kbps != 0U) && (hz != 0U));
        offset += (size_t)(((mpeg1 ? 144U : 72U) * kbps * 1000U) / hz + padding);
    }
    return offset;
}

/**
 * @brief 把一段字节按 chunk 分块交入播放模块并推进到结束。
 * @param[in]  data        码流（已去掉标签）。
 * @param[in]  size        码流字节数。
 * @param[in]  audio_bytes BeginStream 的音频区间字节数。
 * @param[in]  chunk       每次尝试交入的字节数；0 表示每次交入全部剩余。
 * @param[out] run         观察结果。
 * @note  数据全部被窗口接收后才 FinishInput，模拟播放任务在流记录 ENDED 之后的告知。
 */
void test_run_stream(const uint8_t *data, size_t size, uint32_t audio_bytes, uint32_t chunk, test_run_t *run)
{
    size_t offset = 0U;
    int input_finished = 0;
    size_t guard = (size * 2U) + 200000U;
    int16_t stereo[PLAYBACK_SERVICE_FRAME_MAX_SAMPLES * 2U];
    int first_seen = 0;

    memset(run, 0, sizeof(*run));
    run->PcmHash = 1469598103934665603ULL;
    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_BeginStream(audio_bytes) == SERVICE_OK);

    while (guard-- > 0U)
    {
        Service_Playback_AdvanceReportTypeDef report;

        if (!input_finished)
        {
            if (offset < size)
            {
                uint32_t accepted = 0U;
                size_t want = size - offset;

                if ((chunk != 0U) && (want > chunk))
                {
                    want = chunk;
                }
                assert(Service_Playback_Feed(data + offset, (uint32_t)want, &accepted) == SERVICE_OK);
                assert(accepted <= want);
                offset += accepted;
            }
            if (offset >= size)
            {
                assert(Service_Playback_FinishInput() == SERVICE_OK);
                input_finished = 1;
            }
        }

        assert(Service_Playback_Advance(&report) == SERVICE_OK);

        if (report.DecodedMs < run->LastDecodedMs)
        {
            run->TimeWentBackwards++;
        }
        run->LastDecodedMs = report.DecodedMs;
        if (report.DecodedMs > run->MaxDecodedMs)
        {
            run->MaxDecodedMs = report.DecodedMs;
        }

        if (report.FirstFrame)
        {
            run->FirstFrameReports++;
            run->FirstFrameAtResult = (uint32_t)report.Result;
            run->StreamInfo = report.StreamInfo;
            first_seen = 1;
        }

        switch (report.Result)
        {
        case SERVICE_PLAYBACK_ADVANCE_FRAME:
        {
            uint32_t frames = 0U;
            uint32_t i;

            run->FrameResults++;
            if (Service_Playback_ReadFrame(stereo, PLAYBACK_SERVICE_FRAME_MAX_SAMPLES, &frames) != SERVICE_OK)
            {
                run->ReadFrameFailures++;
                break;
            }
            assert(frames == report.PcmFrames);
            run->StereoFrames += frames;
            run->Frames1152 += (frames == 1152U) ? 1U : 0U;
            run->Frames576 += (frames == 576U) ? 1U : 0U;
            for (i = 0U; i < frames; i++)
            {
                const int left = stereo[2U * i];
                const int right = stereo[(2U * i) + 1U];
                const int la = (left < 0) ? -left : left;
                const int ra = (right < 0) ? -right : right;

                if (left != right)
                {
                    run->LeftRightDiffer++;
                }
                if (la > run->PeakAbs)
                {
                    run->PeakAbs = la;
                }
                if (ra > run->PeakAbs)
                {
                    run->PeakAbs = ra;
                }
            }
            for (i = 0U; i < frames * 2U; i++)
            {
                run->PcmHash = (run->PcmHash ^ (uint16_t)stereo[i]) * 1099511628211ULL;
            }
            break;
        }
        case SERVICE_PLAYBACK_ADVANCE_SKIPPED:
            run->SkippedResults++;
            if (!first_seen)
            {
                run->SkippedBeforeFirst++;
            }
            break;
        case SERVICE_PLAYBACK_ADVANCE_NEED_INPUT:
            run->NeedInputResults++;
            assert(!input_finished); /* 输入结束后不应再要输入。 */
            break;
        case SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED:
        case SERVICE_PLAYBACK_ADVANCE_END:
            run->FinalResult = report.Result;
            return;
        default:
            assert(0);
            break;
        }
    }

    assert(!"整流驱动未在上界内结束");
}
