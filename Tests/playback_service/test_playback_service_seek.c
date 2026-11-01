/**
 * @file test_playback_service_seek.c
 * @brief 播放模块的毫秒到字节映射与从落点重开（#175，0.9.0 真跳转的 Service 半边）。
 * @note  真实 minimp3 + fixtures/xing/ 的四个 20 s 精度夹具（#174）。精度判据独立于产品：
 *        测试自己沿帧头逐帧扫描，算出偏移处第一帧的真实时间，再与映射反算的落点比较。
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Tests/playback_service/fake_platform_audio.h"
#include "Tests/playback_service/playback_service_test_support.h"

#define FRAME_SAMPLES   (1152U)
#define SAMPLE_RATE_HZ  (44100U)
#define FRAME_US        (26122U) /* 1152 / 44100 s，向下取整的微秒。 */
/* 重开后因位储备不足（main_data_begin 最多 511 B）被丢弃的帧数上界：夹具实测最多 4 帧（低码率 VBR 的小帧），
 * 比 #161 的“CBR 128 kbps 2 帧”推断更大（64 kbps 下 511 B 位储备跨 2.5 帧）。 */
#define MAX_RESYNC_DROPPED_FRAMES (4U)

/**
 * @brief 夹具里第 index 个音频帧（不含 Xing/Info 头帧）的真实起始时间。
 * @param[in] index 音频帧序号。
 * @return 毫秒（向下取整）。
 */
static uint32_t frame_index_to_ms(uint32_t index)
{
    return (uint32_t)(((uint64_t)index * FRAME_SAMPLES * 1000ULL) / SAMPLE_RATE_HZ);
}

/**
 * @brief 逐帧扫描：offset 处（或其后）第一个音频帧的真实起始时间。
 * @param[in] blob   夹具文件内容（无 ID3，音频区间 = 整个文件）。
 * @param[in] offset 相对音频区间起点的字节偏移。
 * @return 毫秒；offset 在最后一帧之后时返回全部音频帧的总时长。
 * @note  头帧（首帧带 Xing/Info 标识）不计入时间：音频时间从其后的第一个音频帧起算。
 */
static uint32_t scan_first_frame_ms_at(const test_blob_t *blob, size_t offset)
{
    size_t pos = 0U;
    uint32_t index = 0U;

    if ((memcmp(blob->Data + 36U, "Xing", 4U) == 0) || (memcmp(blob->Data + 36U, "Info", 4U) == 0))
    {
        pos = test_frame_end_offset(blob->Data, 0U, 1U);
    }
    while (pos < blob->Size)
    {
        if (pos >= offset)
        {
            return frame_index_to_ms(index);
        }
        pos = test_frame_end_offset(blob->Data, pos, 1U);
        index++;
    }
    return frame_index_to_ms(index);
}

/**
 * @brief 逐帧扫描：offset 处（或其后）起到文件尾共有多少个音频帧（头帧不计）。
 * @param[in] blob   夹具文件内容。
 * @param[in] offset 相对音频区间起点的字节偏移。
 * @return 帧数。
 */
static uint32_t scan_frames_from(const test_blob_t *blob, size_t offset)
{
    size_t pos = 0U;
    uint32_t count = 0U;

    if ((memcmp(blob->Data + 36U, "Xing", 4U) == 0) || (memcmp(blob->Data + 36U, "Info", 4U) == 0))
    {
        pos = test_frame_end_offset(blob->Data, 0U, 1U);
    }
    while (pos < blob->Size)
    {
        if (pos >= offset)
        {
            count++;
        }
        pos = test_frame_end_offset(blob->Data, pos, 1U);
    }
    return count;
}

/**
 * @brief 开一条流并解到首帧，使总时长等流级事实就绪；音频区间取整个文件。
 * @param[in]  blob 夹具。
 * @param[out] info 首帧信息。
 */
static void prime_stream(const test_blob_t *blob, Service_Playback_StreamInfoTypeDef *info)
{
    test_feeder_t feeder;
    int guard;

    test_begin_stream(blob, blob->Size, 0U, &feeder);
    for (guard = 0; guard < 1000; guard++)
    {
        Service_Playback_AdvanceReportTypeDef report;

        test_feeder_feed(&feeder);
        assert(Service_Playback_Advance(&report) == SERVICE_OK);
        if (report.FirstFrame)
        {
            *info = report.StreamInfo;
            return;
        }
    }
    assert(!"首帧没有出现");
}

/**
 * @brief 目标毫秒 → 落点的绝对误差（毫秒）与真实值对比。
 * @param[in]  blob   夹具。
 * @param[in]  point  映射结果。
 * @param[out] err_ms 落点与偏移处第一帧真实时间之差的绝对值。
 */
static void landing_error_ms(const test_blob_t *blob, const Service_Playback_SeekPointTypeDef *point, uint32_t *err_ms)
{
    const uint32_t truth = scan_first_frame_ms_at(blob, point->AudioOffset);

    *err_ms = (truth > point->LandingMs) ? (truth - point->LandingMs) : (point->LandingMs - truth);
}

/**
 * @brief CBR（无头与带 Info 无 TOC）按 AudioBytes 线性映射，落点误差不超过 3 帧。
 */
static void test_cbr_linear_mapping_is_within_three_frames(void)
{
    static const char *const names[] = {"xing/cbr_nohdr.mp3", "xing/cbr_info.mp3"};
    size_t n;

    for (n = 0U; n < (sizeof(names) / sizeof(names[0])); n++)
    {
        test_blob_t blob;
        Service_Playback_StreamInfoTypeDef info;
        uint32_t target;
        uint32_t worst = 0U;

        test_load_fixture(names[n], &blob);
        prime_stream(&blob, &info);
        assert(info.TotalDurationMs == 20035U);

        for (target = 0U; target < (info.TotalDurationMs - 1000U); target += 997U)
        {
            Service_Playback_SeekPointTypeDef point;
            uint32_t err;

            assert(Service_Playback_MapSeek(target, &point) == SERVICE_OK);
            assert(point.Map == SERVICE_PLAYBACK_SEEK_MAP_LIN);
            assert(!point.Clamped);
            landing_error_ms(&blob, &point, &err);
            assert((err * 1000U) <= (3U * FRAME_US));
            if (err > worst)
            {
                worst = err;
            }
        }
        (void)printf("%s: CBR linear worst landing error %u ms\n", names[n], worst);
        test_free_blob(&blob);
    }
}

/**
 * @brief 带 TOC 的 VBR 按 TOC 映射，落点误差不超过 总时长/100 + 3 帧；同一段 VBR 音频无头时退回线性并只记录误差。
 */
static void test_vbr_toc_mapping_and_headerless_vbr(void)
{
    test_blob_t toc_blob;
    test_blob_t lin_blob;
    Service_Playback_StreamInfoTypeDef info;
    uint32_t target;
    uint32_t worst_toc = 0U;
    uint32_t worst_lin = 0U;
    uint32_t last_landing;

    test_load_fixture("xing/vbr_toc.mp3", &toc_blob);
    prime_stream(&toc_blob, &info);
    assert(info.TotalDurationMs == 20035U);
    last_landing = 0U;
    for (target = 0U; target < (info.TotalDurationMs - 1000U); target += 499U)
    {
        Service_Playback_SeekPointTypeDef point;
        uint32_t err;

        assert(Service_Playback_MapSeek(target, &point) == SERVICE_OK);
        assert(point.Map == SERVICE_PLAYBACK_SEEK_MAP_TOC);
        assert(!point.Clamped);
        assert(point.LandingMs >= last_landing);
        last_landing = point.LandingMs;
        landing_error_ms(&toc_blob, &point, &err);
        assert(((err * 1000U) <= ((info.TotalDurationMs * 10U) + (3U * FRAME_US))));
        if (err > worst_toc)
        {
            worst_toc = err;
        }
    }
    (void)printf("vbr_toc: TOC worst landing error %u ms (limit %u ms)\n", worst_toc,
                 (info.TotalDurationMs / 100U) + ((3U * FRAME_US) / 1000U));
    test_free_blob(&toc_blob);

    test_load_fixture("xing/vbr_nohdr.mp3", &lin_blob);
    prime_stream(&lin_blob, &info);
    /* 无头：总时长按首帧比特率估算（VBR 下可能严重偏离），映射只能线性，误差只记录。 */
    last_landing = 0U;
    for (target = 0U; target < info.TotalDurationMs; target += 499U)
    {
        Service_Playback_SeekPointTypeDef point;
        uint32_t err;

        assert(Service_Playback_MapSeek(target, &point) == SERVICE_OK);
        assert(point.Map == SERVICE_PLAYBACK_SEEK_MAP_LIN);
        assert(point.LandingMs >= last_landing);
        last_landing = point.LandingMs;
        landing_error_ms(&lin_blob, &point, &err);
        if (err > worst_lin)
        {
            worst_lin = err;
        }
    }
    (void)printf("vbr_nohdr: linear worst landing error %u ms (recorded only, total %u ms)\n", worst_lin, info.TotalDurationMs);
    test_free_blob(&lin_blob);
}

/**
 * @brief VBRI 一律线性；TOC 全零或乱序视为不可用，退回线性（用内存里改过的 vbr_toc 副本构造）。
 */
static void test_vbri_and_unusable_toc_fall_back_to_linear(void)
{
    static const size_t toc_first_byte = 36U + 8U + 4U + 4U; /* Xing 偏移 + 标识与 flags + 帧数 + 字节数。 */
    test_blob_t blob;
    Service_Playback_StreamInfoTypeDef info;
    Service_Playback_SeekPointTypeDef point;
    size_t i;

    test_load_fixture("vbri_44100_stereo.mp3", &blob);
    prime_stream(&blob, &info);
    assert(Service_Playback_MapSeek(info.TotalDurationMs / 2U, &point) == SERVICE_OK);
    assert(point.Map == SERVICE_PLAYBACK_SEEK_MAP_LIN);
    test_free_blob(&blob);

    test_load_fixture("xing/vbr_toc.mp3", &blob);
    assert(memcmp(blob.Data + 36U, "Xing", 4U) == 0);
    for (i = 0U; i < PLAYBACK_SERVICE_TOC_ENTRIES; i++)
    {
        blob.Data[toc_first_byte + i] = 0U;
    }
    prime_stream(&blob, &info);
    assert(Service_Playback_MapSeek(info.TotalDurationMs / 2U, &point) == SERVICE_OK);
    assert(point.Map == SERVICE_PLAYBACK_SEEK_MAP_LIN);
    test_free_blob(&blob);

    test_load_fixture("xing/vbr_toc.mp3", &blob);
    blob.Data[toc_first_byte + 50U] = 0U; /* 第 50 项小于第 49 项：乱序。 */
    prime_stream(&blob, &info);
    assert(Service_Playback_MapSeek(info.TotalDurationMs / 2U, &point) == SERVICE_OK);
    assert(point.Map == SERVICE_PLAYBACK_SEEK_MAP_LIN);
    test_free_blob(&blob);
}

/**
 * @brief 目标 0 落在音频有效区间起点（带头帧时在头帧之后），落点 0；落点随目标单调。
 */
static void test_target_zero_lands_after_header_frame_and_landing_is_monotonic(void)
{
    static const struct
    {
        const char *Name;
        uint32_t AudioStart;
    } cases[] = {
        {"xing/cbr_nohdr.mp3", 0U},
        {"xing/cbr_info.mp3", 417U},
        {"xing/vbr_toc.mp3", 417U},
        {"xing/vbr_nohdr.mp3", 0U},
    };
    size_t n;

    for (n = 0U; n < (sizeof(cases) / sizeof(cases[0])); n++)
    {
        test_blob_t blob;
        Service_Playback_StreamInfoTypeDef info;
        Service_Playback_SeekPointTypeDef point;
        uint32_t target;
        uint32_t last_offset = 0U;
        uint32_t last_landing = 0U;

        test_load_fixture(cases[n].Name, &blob);
        prime_stream(&blob, &info);
        assert(Service_Playback_MapSeek(0U, &point) == SERVICE_OK);
        assert(point.AudioOffset == cases[n].AudioStart);
        assert(point.LandingMs == 0U);
        assert(!point.Clamped);

        for (target = 0U; target <= (info.TotalDurationMs + 3000U); target += 61U)
        {
            assert(Service_Playback_MapSeek(target, &point) == SERVICE_OK);
            assert(point.AudioOffset >= last_offset);
            assert(point.LandingMs >= last_landing);
            assert(point.AudioOffset <= (uint32_t)blob.Size);
            assert(point.LandingMs <= info.TotalDurationMs);
            last_offset = point.AudioOffset;
            last_landing = point.LandingMs;
        }
        test_free_blob(&blob);
    }
}

/**
 * @brief 越尾钳位：偏移钳到 AudioBytes − W_min 并报告 Clamped，落点是该偏移的反算，且不超过总时长。
 */
static void test_past_end_target_clamps_to_audio_bytes_minus_wmin(void)
{
    static const char *const names[] = {
        "xing/cbr_nohdr.mp3", "xing/cbr_info.mp3", "xing/vbr_toc.mp3", "xing/vbr_nohdr.mp3",
        "cbr_44100_stereo.mp3", "vbr_xing_44100_stereo.mp3", "vbri_44100_stereo.mp3",
    };
    size_t n;

    for (n = 0U; n < (sizeof(names) / sizeof(names[0])); n++)
    {
        test_blob_t blob;
        Service_Playback_StreamInfoTypeDef info;
        Service_Playback_SeekPointTypeDef at_total;
        Service_Playback_SeekPointTypeDef beyond;
        Service_Playback_SeekPointTypeDef far_beyond;

        test_load_fixture(names[n], &blob);
        prime_stream(&blob, &info);
        assert(Service_Playback_MapSeek(info.TotalDurationMs, &at_total) == SERVICE_OK);
        assert(Service_Playback_MapSeek(info.TotalDurationMs + 7000U, &beyond) == SERVICE_OK);
        assert(Service_Playback_MapSeek(UINT32_MAX, &far_beyond) == SERVICE_OK);

        assert(at_total.Clamped && beyond.Clamped && far_beyond.Clamped);
        assert(at_total.AudioOffset == ((uint32_t)blob.Size - PLAYBACK_SERVICE_WINDOW_MIN_BYTES));
        assert(beyond.AudioOffset == at_total.AudioOffset);
        assert(far_beyond.AudioOffset == at_total.AudioOffset);
        assert(beyond.LandingMs == at_total.LandingMs);
        assert(at_total.LandingMs <= info.TotalDurationMs);
        assert(at_total.LandingMs > 0U);
        test_free_blob(&blob);
    }
}

/**
 * @brief 流级事实未知时不能映射：首帧之前、无流、总时长未知（无头且 audio_bytes 为 0）、空指针。
 */
static void test_map_refuses_when_stream_facts_are_unknown(void)
{
    test_blob_t blob;
    test_feeder_t feeder;
    Service_Playback_SeekPointTypeDef point;
    Service_Playback_AdvanceReportTypeDef report;
    int guard;

    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_MapSeek(1000U, &point) == SERVICE_NOT_READY);

    test_load_fixture("xing/cbr_nohdr.mp3", &blob);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    assert(Service_Playback_MapSeek(1000U, &point) == SERVICE_NOT_READY); /* 还没有首帧。 */
    assert(Service_Playback_MapSeek(1000U, NULL) == SERVICE_INVALID_PARAM);

    /* audio_bytes 为 0 且无流头：总时长未知。 */
    assert(Service_Playback_BeginStream(0U) == SERVICE_OK);
    test_feeder_init(&feeder, blob.Data, blob.Size, 0U);
    for (guard = 0; guard < 50; guard++)
    {
        test_feeder_feed(&feeder);
        assert(Service_Playback_Advance(&report) == SERVICE_OK);
        if (report.FirstFrame)
        {
            break;
        }
    }
    assert(report.FirstFrame);
    assert(report.StreamInfo.TotalDurationMs == 0U);
    assert(Service_Playback_MapSeek(1000U, &point) == SERVICE_NOT_READY);
    test_free_blob(&blob);
}

/**
 * @brief 把 blob 从 offset 起的字节交入重开后的流并读尽，用解码半边推进到 END，返回观察结果。
 * @param[in]  blob   夹具。
 * @param[in]  offset 起始字节偏移（Service_Playback_MapSeek 的 AudioOffset）。
 * @param[out] run    观察结果；FirstFrameReports 应为 0（首帧信息已保留）。
 */
static void drain_reopened_stream(const test_blob_t *blob, size_t offset, test_run_t *run)
{
    size_t fed = offset;
    int16_t stereo[PLAYBACK_SERVICE_FRAME_MAX_SAMPLES * 2U];
    int guard;

    memset(run, 0, sizeof(*run));
    for (guard = 0; guard < 200000; guard++)
    {
        Service_Playback_AdvanceReportTypeDef report;
        uint32_t frames = 0U;

        if (fed < blob->Size)
        {
            uint32_t accepted = 0U;

            assert(Service_Playback_Feed(blob->Data + fed, (uint32_t)(blob->Size - fed), &accepted) == SERVICE_OK);
            fed += accepted;
        }
        if (fed >= blob->Size)
        {
            assert(Service_Playback_FinishInput() == SERVICE_OK);
        }
        assert(Service_Playback_Advance(&report) == SERVICE_OK);
        if (run->FrameResults == 0U)
        {
            run->LastDecodedMs = report.DecodedMs;
            if (report.Result == SERVICE_PLAYBACK_ADVANCE_FRAME)
            {
                run->MaxDecodedMs = report.DecodedMs; /* 借用：首个 PCM 帧之后的已解码时间。 */
            }
        }
        if (report.DecodedMs < run->LastDecodedMs)
        {
            run->TimeWentBackwards++;
        }
        run->LastDecodedMs = report.DecodedMs;
        run->FirstFrameReports += report.FirstFrame ? 1U : 0U;
        if (report.Result == SERVICE_PLAYBACK_ADVANCE_FRAME)
        {
            assert(Service_Playback_ReadFrame(stereo, PLAYBACK_SERVICE_FRAME_MAX_SAMPLES, &frames) == SERVICE_OK);
            run->FrameResults++;
            run->StereoFrames += frames;
        }
        if ((report.Result == SERVICE_PLAYBACK_ADVANCE_END) || (report.Result == SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED))
        {
            run->FinalResult = report.Result;
            return;
        }
    }
    assert(!"重开后的流没有结束");
}

/**
 * @brief 从落点重开：保留总时长、采样率与 TOC，重新同步出 PCM，已解码时间从落点起算且单调，读到尽头 END。
 */
static void test_reopen_from_landing_resyncs_and_keeps_stream_facts(void)
{
    static const char *const names[] = {"xing/cbr_nohdr.mp3", "xing/cbr_info.mp3", "xing/vbr_toc.mp3", "xing/vbr_nohdr.mp3"};
    static const uint32_t targets[] = {0U, 1U, 5000U, 10017U, 19000U};
    size_t n;
    size_t t;

    for (n = 0U; n < (sizeof(names) / sizeof(names[0])); n++)
    {
        for (t = 0U; t < (sizeof(targets) / sizeof(targets[0])); t++)
        {
            test_blob_t blob;
            Service_Playback_StreamInfoTypeDef info;
            Service_Playback_SeekPointTypeDef point;
            Service_Playback_SeekPointTypeDef again;
            test_run_t run;
            uint32_t remaining_frames;

            test_load_fixture(names[n], &blob);
            prime_stream(&blob, &info);
            if (info.TotalDurationMs <= targets[t])
            {
                test_free_blob(&blob);
                continue;
            }
            assert(Service_Playback_MapSeek(targets[t], &point) == SERVICE_OK);

            assert(Service_Playback_ReopenStream(point.LandingMs) == SERVICE_OK);
            drain_reopened_stream(&blob, point.AudioOffset, &run);

            assert(run.FirstFrameReports == 0U); /* 首帧信息已保留，不再回报。 */
            assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
            assert(run.FrameResults > 0U);
            assert(run.TimeWentBackwards == 0U);
            assert(run.MaxDecodedMs >= point.LandingMs); /* 已解码时间从落点起算。 */
            assert(run.LastDecodedMs <= info.TotalDurationMs);
            /* 剩余帧数以夹具扫描为准（头帧不计）；位储备不足最多丢弃头 2 帧（#161）。 */
            remaining_frames = scan_frames_from(&blob, point.AudioOffset);
            assert((run.FrameResults + MAX_RESYNC_DROPPED_FRAMES) >= remaining_frames);
            assert(run.FrameResults <= remaining_frames);

            /* 流级事实保留：重开后映射结果不变（含 TOC 与映射方式）。 */
            assert(Service_Playback_MapSeek(targets[t], &again) == SERVICE_OK);
            assert(again.AudioOffset == point.AudioOffset);
            assert(again.LandingMs == point.LandingMs);
            assert(again.Map == point.Map);
            test_free_blob(&blob);
        }
    }
}

/**
 * @brief 重开后经输出管线（Pump）从落点放到流尾：Stop 旧输出、不再回报首帧、放完回报 StreamEnded。
 */
static void test_reopen_through_output_pipeline_plays_from_landing_to_end(void)
{
    test_blob_t blob;
    test_feeder_t feeder;
    test_facts_t facts;
    Service_Playback_SeekPointTypeDef point;
    Service_Playback_PumpReportTypeDef report;
    uint32_t stops_before;
    uint32_t rounds;
    size_t played_samples = 0U;

    test_load_fixture("xing/vbr_toc.mp3", &blob);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    memset(&facts, 0, sizeof(facts));
    for (rounds = 0U; (rounds < 50U) && (facts.Started == 0U); rounds++)
    {
        assert(test_pump(&feeder, &facts, &report) == SERVICE_OK);
    }
    assert(facts.Started == 1U);
    assert(facts.FirstFrames == 1U);
    assert(g_fake_audio.Running);

    assert(Service_Playback_MapSeek(12345U, &point) == SERVICE_OK);
    stops_before = g_fake_audio.StopCount;
    assert(Service_Playback_ReopenStream(point.LandingMs) == SERVICE_OK);
    assert(g_fake_audio.StopCount == (stops_before + 1U)); /* 与切歌一致：先停输出。 */
    assert(!g_fake_audio.Running);

    memset(&facts, 0, sizeof(facts));
    test_feeder_init(&feeder, blob.Data + point.AudioOffset, blob.Size - point.AudioOffset, 0U);
    assert(test_play_until_ended(&feeder, &facts, 4000U) == 1);
    assert(facts.Ended >= 1U);
    assert(facts.FirstFrames == 0U);
    assert(facts.SyncExhausted == 0U);
    assert(facts.Started >= 1U);
    assert(facts.TimeWentBackwards == 0U);
    assert(facts.LastDecodedMs >= point.LandingMs);
    assert(facts.LastDecodedMs <= 20035U);
    (void)fake_audio_played(&played_samples);
    assert(played_samples > 0U);
    assert(g_fake_audio.ContractViolations == 0U);
    test_free_blob(&blob);
}

/**
 * @brief 重开的前提与生命周期：首帧之前、BeginStream 之后没有流级事实可保留；读尽后、放弃后仍可重开并重新接收输入。
 */
static void test_reopen_lifecycle(void)
{
    test_blob_t blob;
    test_feeder_t feeder;
    Service_Playback_StreamInfoTypeDef info;
    Service_Playback_SeekPointTypeDef point;
    test_run_t run;
    test_run_t drained;
    uint32_t accepted = 0U;

    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_ReopenStream(0U) == SERVICE_NOT_READY); /* 没有任何流。 */

    test_load_fixture("xing/cbr_info.mp3", &blob);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    assert(Service_Playback_ReopenStream(0U) == SERVICE_NOT_READY); /* 首帧还没解出。 */

    /* 整条流读到 END 之后重开：输入读尽标志被清除，可以再交字节。 */
    test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 0U, &run);
    assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
    assert(Service_Playback_Feed(blob.Data, 16U, &accepted) == SERVICE_NOT_READY);
    assert(Service_Playback_MapSeek(10000U, &point) == SERVICE_OK);
    assert(Service_Playback_ReopenStream(point.LandingMs) == SERVICE_OK);
    assert(Service_Playback_Feed(blob.Data + point.AudioOffset, 16U, &accepted) == SERVICE_OK);
    assert(accepted == 16U);

    /* 放弃之后重开：流级事实仍在。 */
    assert(Service_Playback_AbandonStream() == SERVICE_OK);
    assert(Service_Playback_Feed(blob.Data, 16U, &accepted) == SERVICE_NOT_READY);
    assert(Service_Playback_MapSeek(10000U, &point) == SERVICE_OK);
    assert(Service_Playback_ReopenStream(point.LandingMs) == SERVICE_OK);
    drain_reopened_stream(&blob, point.AudioOffset, &drained);
    assert(drained.FrameResults > 0U);
    assert(drained.FirstFrameReports == 0U);

    /* 落点超过总时长按总时长算；已解码时间不超过总时长。 */
    assert(Service_Playback_ReopenStream(UINT32_MAX) == SERVICE_OK);
    drain_reopened_stream(&blob, blob.Size - 1000U, &drained);
    assert(drained.LastDecodedMs == 20035U);

    /* BeginStream 重新开始：旧流的事实作废。 */
    assert(Service_Playback_BeginStream((uint32_t)blob.Size) == SERVICE_OK);
    assert(Service_Playback_ReopenStream(0U) == SERVICE_NOT_READY);
    assert(Service_Playback_MapSeek(0U, &point) == SERVICE_NOT_READY);
    prime_stream(&blob, &info);
    assert(Service_Playback_MapSeek(0U, &point) == SERVICE_OK);
    test_free_blob(&blob);
}

/**
 * @brief 拉起一条流并经 Pump 解到首帧，使流级事实就绪（输出管线路径）。
 * @param[in]  blob   夹具。
 * @param[out] feeder 喂入器（已喂入若干块）。
 */
static void prime_stream_via_pump(const test_blob_t *blob, test_feeder_t *feeder)
{
    test_facts_t facts;
    int guard;

    test_begin_stream(blob, blob->Size, 0U, feeder);
    memset(&facts, 0, sizeof(facts));
    for (guard = 0; (guard < 50) && (facts.FirstFrames == 0U); guard++)
    {
        Service_Playback_PumpReportTypeDef report;

        assert(test_pump(feeder, &facts, &report) == SERVICE_OK);
    }
    assert(facts.FirstFrames == 1U);
}

/**
 * @brief #166 第 11 条：钳到 AudioBytes − W_min 的重开流，一帧都解不出也不会被判成坏文件。
 * @note  新开的流（BeginStream）读尽仍无首帧会回报 SyncExhausted（坏文件）；重开的流首帧已确认，
 *        读尽后按流尾放完处理。同时核实钳位位置上夹具实际能解出至少一帧。
 */
static void test_clamped_tail_reopen_is_not_judged_a_bad_file(void)
{
    static const char *const names[] = {
        "xing/cbr_nohdr.mp3", "xing/cbr_info.mp3", "xing/vbr_toc.mp3", "xing/vbr_nohdr.mp3",
        "cbr_44100_stereo.mp3", "vbr_xing_44100_stereo.mp3", "vbri_44100_stereo.mp3",
    };
    uint8_t garbage[PLAYBACK_SERVICE_WINDOW_MIN_BYTES];
    size_t n;
    test_blob_t blob;
    test_feeder_t feeder;
    test_facts_t facts;

    /* 对照：新流只有垃圾字节，读尽 -> 坏文件。 */
    test_fill_garbage(garbage, sizeof(garbage), 7U);
    test_load_fixture("xing/cbr_info.mp3", &blob);
    test_begin_stream(&blob, blob.Size, 0U, &feeder);
    test_feeder_init(&feeder, garbage, sizeof(garbage), 0U);
    memset(&facts, 0, sizeof(facts));
    assert(test_play_until_ended(&feeder, &facts, 100U) == 0);
    assert(facts.SyncExhausted >= 1U);
    assert(facts.Ended == 0U);

    /* 重开的流同样的垃圾 -> 正常放完。 */
    prime_stream_via_pump(&blob, &feeder);
    assert(Service_Playback_ReopenStream(10000U) == SERVICE_OK);
    test_feeder_init(&feeder, garbage, sizeof(garbage), 0U);
    memset(&facts, 0, sizeof(facts));
    assert(test_play_until_ended(&feeder, &facts, 100U) == 1);
    assert(facts.SyncExhausted == 0U);
    test_free_blob(&blob);

    for (n = 0U; n < (sizeof(names) / sizeof(names[0])); n++)
    {
        Service_Playback_StreamInfoTypeDef info;
        Service_Playback_SeekPointTypeDef point;
        test_run_t run;

        test_load_fixture(names[n], &blob);
        prime_stream(&blob, &info);
        assert(Service_Playback_MapSeek(UINT32_MAX, &point) == SERVICE_OK);
        assert(point.Clamped);

        /* 钳位处解码半边：读尽 END，至少解出一帧。 */
        assert(Service_Playback_ReopenStream(point.LandingMs) == SERVICE_OK);
        drain_reopened_stream(&blob, point.AudioOffset, &run);
        assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
        assert(run.FrameResults >= 1U);

        /* 钳位处输出管线：放完，不是坏文件。 */
        assert(Service_Playback_ReopenStream(point.LandingMs) == SERVICE_OK);
        test_feeder_init(&feeder, blob.Data + point.AudioOffset, blob.Size - point.AudioOffset, 0U);
        memset(&facts, 0, sizeof(facts));
        assert(test_play_until_ended(&feeder, &facts, 200U) == 1);
        assert(facts.SyncExhausted == 0U);
        assert(facts.Ended >= 1U);
        test_free_blob(&blob);
    }
}

/**
 * @brief 钳位窗口 W_min 在任意帧对齐下都含可解帧（夹具的帧长 ≤ 417 B）：
 *        从流中段取每个起点的 W_min 字节窗口当作截断的流尾，重开后至少解出一帧。
 */
static void test_every_alignment_of_a_wmin_tail_decodes_a_frame(void)
{
    static const char *const names[] = {"xing/cbr_nohdr.mp3", "xing/vbr_nohdr.mp3", "cbr_44100_stereo.mp3"};
    size_t n;

    for (n = 0U; n < (sizeof(names) / sizeof(names[0])); n++)
    {
        test_blob_t blob;
        Service_Playback_StreamInfoTypeDef info;
        size_t start;
        uint32_t zero_frame_windows = 0U;

        test_load_fixture(names[n], &blob);
        prime_stream(&blob, &info);
        for (start = 3000U; start < (3000U + 1500U); start++)
        {
            test_blob_t window;
            test_run_t run;

            window.Data = blob.Data + start;
            window.Size = PLAYBACK_SERVICE_WINDOW_MIN_BYTES;
            assert(Service_Playback_ReopenStream(0U) == SERVICE_OK);
            drain_reopened_stream(&window, 0U, &run);
            assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
            if (run.FrameResults == 0U)
            {
                zero_frame_windows++;
            }
        }
        assert(zero_frame_windows == 0U);
        test_free_blob(&blob);
    }
}

/**
 * @brief 测试入口。
 * @return 0 表示全部通过；断言失败会直接终止进程。
 */
int main(void)
{
    test_cbr_linear_mapping_is_within_three_frames();
    test_vbr_toc_mapping_and_headerless_vbr();
    test_vbri_and_unusable_toc_fall_back_to_linear();
    test_target_zero_lands_after_header_frame_and_landing_is_monotonic();
    test_past_end_target_clamps_to_audio_bytes_minus_wmin();
    test_map_refuses_when_stream_facts_are_unknown();
    test_reopen_from_landing_resyncs_and_keeps_stream_facts();
    test_reopen_through_output_pipeline_plays_from_landing_to_end();
    test_reopen_lifecycle();
    test_clamped_tail_reopen_is_not_judged_a_bad_file();
    test_every_alignment_of_a_wmin_tail_decodes_a_frame();
    (void)printf("playback_service_seek: OK\n");
    return 0;
}
