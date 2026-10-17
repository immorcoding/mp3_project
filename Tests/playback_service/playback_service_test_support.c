/**
 * @file playback_service_test_support.c
 * @brief 播放模块主机测试的共用夹具读取与整流驱动。
 */

#include "Tests/playback_service/playback_service_test_support.h"
#include "Tests/playback_service/fake_platform_audio.h"

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

/**
 * @brief 用解码半边的 Advance/ReadFrame 得到一份参考 PCM，作为输出管线的独立期望。
 * @param[in]  data        码流。
 * @param[in]  size        码流字节数。
 * @param[in]  audio_bytes BeginStream 的音频区间字节数。
 * @param[out] out         参考 PCM；用 test_free_pcm() 释放。
 */
void test_decode_reference(const uint8_t *data, size_t size, uint32_t audio_bytes, test_pcm_t *out)
{
    size_t offset = 0U;
    size_t capacity = 4096U;
    int16_t stereo[PLAYBACK_SERVICE_FRAME_MAX_SAMPLES * 2U];

    out->Pcm = (int16_t *)malloc(capacity * 2U * sizeof(int16_t));
    assert(out->Pcm != NULL);
    out->Frames = 0U;
    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_BeginStream(audio_bytes) == SERVICE_OK);

    for (;;)
    {
        Service_Playback_AdvanceReportTypeDef report;
        uint32_t accepted = 0U;

        if (offset < size)
        {
            assert(Service_Playback_Feed(data + offset, (uint32_t)(size - offset), &accepted) == SERVICE_OK);
            offset += accepted;
            if (offset >= size)
            {
                assert(Service_Playback_FinishInput() == SERVICE_OK);
            }
        }
        assert(Service_Playback_Advance(&report) == SERVICE_OK);
        if (report.Result == SERVICE_PLAYBACK_ADVANCE_END)
        {
            break;
        }
        assert(report.Result != SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED);
        if (report.Result == SERVICE_PLAYBACK_ADVANCE_FRAME)
        {
            uint32_t frames = 0U;

            assert(Service_Playback_ReadFrame(stereo, PLAYBACK_SERVICE_FRAME_MAX_SAMPLES, &frames) == SERVICE_OK);
            if ((out->Frames + frames) > capacity)
            {
                capacity *= 2U;
                out->Pcm = (int16_t *)realloc(out->Pcm, capacity * 2U * sizeof(int16_t));
                assert(out->Pcm != NULL);
            }
            memcpy(out->Pcm + (out->Frames * 2U), stereo, (size_t)frames * 2U * sizeof(int16_t));
            out->Frames += frames;
        }
    }
}

/**
 * @brief 释放 test_decode_reference() 的结果。
 * @param[in,out] pcm 被释放并清零。
 */
void test_free_pcm(test_pcm_t *pcm)
{
    free(pcm->Pcm);
    pcm->Pcm = NULL;
    pcm->Frames = 0U;
}

/**
 * @brief 初始化喂入器。
 * @param[out] feeder 喂入器。
 * @param[in]  data   码流。
 * @param[in]  size   码流字节数。
 * @param[in]  chunk  每次尝试交入的字节数；0 表示尽量多。
 */
void test_feeder_init(test_feeder_t *feeder, const uint8_t *data, size_t size, uint32_t chunk)
{
    memset(feeder, 0, sizeof(*feeder));
    feeder->Data = data;
    feeder->Size = size;
    feeder->Chunk = chunk;
}

/**
 * @brief 把码流尽量多地交入播放模块；全部交完后告知输入读尽（设置了 Limit 时不告知）。
 * @param[in,out] feeder 喂入器。
 */
void test_feeder_feed(test_feeder_t *feeder)
{
    size_t end = feeder->Size;

    if ((feeder->Limit != 0U) && (end > feeder->Limit))
    {
        end = feeder->Limit;
    }
    while ((feeder->Offset < end) && !feeder->Finished)
    {
        uint32_t accepted = 0U;
        size_t want = end - feeder->Offset;

        if ((feeder->Chunk != 0U) && (want > feeder->Chunk))
        {
            want = feeder->Chunk;
        }
        assert(Service_Playback_Feed(feeder->Data + feeder->Offset, (uint32_t)want, &accepted) == SERVICE_OK);
        if (accepted == 0U)
        {
            break;
        }
        feeder->Offset += accepted;
    }
    if ((feeder->Offset >= feeder->Size) && !feeder->Finished)
    {
        assert(Service_Playback_FinishInput() == SERVICE_OK);
        feeder->Finished = 1;
    }
}

/**
 * @brief 喂入后 Pump 一次，并把报告并入累计事实。
 * @param[in,out] feeder 喂入器。
 * @param[in,out] facts  累计事实。
 * @param[out]    report 本次报告。
 * @return Pump 的状态码。
 */
Service_StatusTypeDef test_pump(test_feeder_t *feeder, test_facts_t *facts, Service_Playback_PumpReportTypeDef *report)
{
    Service_StatusTypeDef status;

    test_feeder_feed(feeder);
    memset(report, 0xA5, sizeof(*report));
    status = Service_Playback_Pump(report);
    if (status != SERVICE_OK)
    {
        return status;
    }

    facts->Pumps++;
    facts->Started += report->OutputStarted ? 1U : 0U;
    facts->Progress += report->OutputProgress ? 1U : 0U;
    facts->Ended += report->StreamEnded ? 1U : 0U;
    facts->FirstFrames += report->FirstFrame ? 1U : 0U;
    facts->NeedInputs += report->NeedInput ? 1U : 0U;
    facts->Underruns += report->Underruns;
    facts->SyncExhausted += report->SyncExhausted ? 1U : 0U;
    facts->StartRejected += report->StartRejected ? 1U : 0U;
    facts->TransferErrors += report->TransferError ? 1U : 0U;
    facts->StopFailed += report->StopFailed ? 1U : 0U;
    if (report->DecodedMs < facts->LastDecodedMs)
    {
        facts->TimeWentBackwards++;
    }
    facts->LastDecodedMs = report->DecodedMs;
    if (report->FirstFrame)
    {
        facts->Info = report->StreamInfo;
    }
    return status;
}

/**
 * @brief 模拟任务循环：喂入、Pump、让 DMA 读完一个半区，直到流尾放完。
 * @param[in,out] feeder     喂入器。
 * @param[in,out] facts      累计事实。
 * @param[in]     max_rounds 最多轮数，防止测试挂死。
 * @return 1 表示收到 StreamEnded；0 表示轮数用尽或出现不可播事实。
 */
int test_play_until_ended(test_feeder_t *feeder, test_facts_t *facts, uint32_t max_rounds)
{
    uint32_t round;

    for (round = 0U; round < max_rounds; round++)
    {
        Service_Playback_PumpReportTypeDef report;

        assert(test_pump(feeder, facts, &report) == SERVICE_OK);
        if (report.StreamEnded)
        {
            return 1;
        }
        if (report.SyncExhausted || report.StartRejected || report.TransferError || report.StopFailed)
        {
            return 0;
        }
        (void)fake_audio_release_half();
    }
    return 0;
}

/**
 * @brief 复位假 Platform，Init 播放模块并开始一条新流。
 * @param[in]  blob   夹具内容。
 * @param[in]  size   交给播放模块的字节数（可小于夹具大小，用于截断出短文件）。
 * @param[in]  chunk  喂入块大小，0 为尽量多。
 * @param[out] feeder 喂入器。
 */
void test_begin_stream(const test_blob_t *blob, size_t size, uint32_t chunk, test_feeder_t *feeder)
{
    fake_audio_reset();
    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_BeginStream((uint32_t)size) == SERVICE_OK);
    test_feeder_init(feeder, blob->Data, size, chunk);
}

/**
 * @brief 断言播放日志按序等于参考 PCM，其后（若有）到 played_frames 为止全是 0。
 * @param[in] ref           参考 PCM。
 * @param[in] played_frames 期望的播放总帧数（不小于参考帧数，多出部分必须是静音）。
 */
void test_assert_played_equals_reference_then_silence(const test_pcm_t *ref, size_t played_frames)
{
    size_t samples = 0U;
    const int16_t *played = fake_audio_played(&samples);
    size_t i;

    assert(samples == played_frames * 2U);
    assert(memcmp(played, ref->Pcm, ref->Frames * 2U * sizeof(int16_t)) == 0);
    for (i = ref->Frames * 2U; i < samples; i++)
    {
        assert(played[i] == 0);
    }
}
