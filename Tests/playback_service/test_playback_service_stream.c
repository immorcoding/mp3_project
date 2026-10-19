/**
 * @file test_playback_service_stream.c
 * @brief 播放模块解码主路径：跨块喂入、W_min 门槛、窗口接收量与流尾截断。
 * @note  真实 minimp3 + 小 MP3 夹具（见 fixtures/README.md）；只经 playback_service.h 公开接口驱动。
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "Tests/playback_service/playback_service_test_support.h"

/* 夹具的音频帧数（不含 Xing/VBRI 元数据帧），以 generate_fixtures.py 的输出为准。 */
#define CBR_AUDIO_FRAMES      (30U)
#define VBR_XING_AUDIO_FRAMES (30U)
#define VBRI_AUDIO_FRAMES     (24U)
#define MPEG2_AUDIO_FRAMES    (41U)

typedef struct
{
    const char *Name;
    uint32_t AudioFrames;
    uint32_t PcmFramesPerFrame;
    int Stereo;
} fixture_case_t;

static const fixture_case_t k_fixtures[] = {
    {"cbr_44100_stereo.mp3", CBR_AUDIO_FRAMES, 1152U, 1},
    {"vbr_xing_44100_stereo.mp3", VBR_XING_AUDIO_FRAMES, 1152U, 1},
    {"vbri_44100_stereo.mp3", VBRI_AUDIO_FRAMES, 1152U, 1},
    {"mpeg2_22050_mono.mp3", MPEG2_AUDIO_FRAMES, 576U, 0},
};

#define FIXTURE_COUNT (sizeof(k_fixtures) / sizeof(k_fixtures[0]))

/**
 * @brief 未 Init、未开始流时，各入口以 SERVICE_NOT_READY 拒绝。
 * @note  必须是进程内第一个用例：Init 之后静态状态不再回到“未初始化”。
 */
static void test_rejects_calls_before_init_and_without_stream(void)
{
    Service_Playback_AdvanceReportTypeDef report;
    uint8_t byte = 0U;
    uint32_t accepted = 7U;

    assert(Service_Playback_BeginStream(1000U) == SERVICE_NOT_READY);
    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_Feed(&byte, 1U, &accepted) == SERVICE_NOT_READY);
    assert(accepted == 0U);
    assert(Service_Playback_FinishInput() == SERVICE_NOT_READY);
    assert(Service_Playback_Advance(&report) == SERVICE_NOT_READY);
    assert(Service_Playback_Feed(NULL, 1U, &accepted) == SERVICE_INVALID_PARAM);
    assert(Service_Playback_Advance(NULL) == SERVICE_INVALID_PARAM);
}

/**
 * @brief 每个夹具无论分块多碎，都解出全部音频帧，且 PCM 逐字节一致、不是静音。
 * @note  分块 1 字节验证 W_min 门槛下反复补读；分块 0 表示尽量多交；313、1000 与窗口容量不整除，
 *        保证块边界落在帧中间。PCM 哈希相同证明跨块喂入没有丢帧、错位或重复。
 */
static void test_every_fixture_decodes_all_frames_for_any_chunking(void)
{
    static const uint32_t chunks[] = {0U, 4096U, 1000U, 313U, 1U};
    size_t f;
    size_t c;

    for (f = 0U; f < FIXTURE_COUNT; f++)
    {
        test_blob_t blob;
        uint64_t reference_hash = 0U;

        test_load_fixture(k_fixtures[f].Name, &blob);
        for (c = 0U; c < (sizeof(chunks) / sizeof(chunks[0])); c++)
        {
            test_run_t run;

            test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, chunks[c], &run);

            assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
            assert(run.FrameResults == k_fixtures[f].AudioFrames);
            assert(run.StereoFrames == (k_fixtures[f].AudioFrames * k_fixtures[f].PcmFramesPerFrame));
            assert(run.ReadFrameFailures == 0);
            assert(run.PeakAbs > 1000); /* 夹具是 12000 峰值的合成音，解成静音说明解码没有真正发生。 */
            if (k_fixtures[f].Stereo)
            {
                assert(run.LeftRightDiffer > 0U);
            }
            else
            {
                assert(run.LeftRightDiffer == 0U); /* 单声道在拷出时复制到左右。 */
            }
            if (c == 0U)
            {
                reference_hash = run.PcmHash;
            }
            else
            {
                assert(run.PcmHash == reference_hash);
            }
        }
        test_free_blob(&blob);
    }
}

/**
 * @brief 交入的字节不足 W_min 且输入未结束时只要求更多输入，不消耗也不解码；补到 W_min 后才推进。
 */
static void test_waits_for_window_minimum_before_decoding(void)
{
    test_blob_t blob;
    Service_Playback_AdvanceReportTypeDef report;
    uint32_t accepted = 0U;
    uint32_t i;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_BeginStream((uint32_t)blob.Size) == SERVICE_OK);

    assert(Service_Playback_Feed(blob.Data, PLAYBACK_SERVICE_WINDOW_MIN_BYTES - 1U, &accepted) == SERVICE_OK);
    assert(accepted == (PLAYBACK_SERVICE_WINDOW_MIN_BYTES - 1U));
    for (i = 0U; i < 3U; i++)
    {
        assert(Service_Playback_Advance(&report) == SERVICE_OK);
        assert(report.Result == SERVICE_PLAYBACK_ADVANCE_NEED_INPUT);
        assert(!report.FirstFrame);
        assert(report.PcmFrames == 0U);
        assert(report.DecodedMs == 0U);
    }

    /* 再补一个字节恰好达到 W_min，随即解出首帧。 */
    assert(Service_Playback_Feed(blob.Data + accepted, 1U, &accepted) == SERVICE_OK);
    assert(accepted == 1U);
    assert(Service_Playback_Advance(&report) == SERVICE_OK);
    assert(report.Result == SERVICE_PLAYBACK_ADVANCE_FRAME);
    assert(report.FirstFrame);
    assert(report.PcmFrames == 1152U);

    test_free_blob(&blob);
}

/**
 * @brief 输入结束后，不足 W_min 的余量原样交给解码器直到耗尽（短文件）。
 */
static void test_decodes_short_remainder_after_input_ended(void)
{
    test_blob_t blob;
    test_run_t run;
    size_t end;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    end = test_frame_end_offset(blob.Data, 0U, 3U);
    assert(end < PLAYBACK_SERVICE_WINDOW_MIN_BYTES);

    test_run_stream(blob.Data, end, (uint32_t)end, 0U, &run);

    assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
    assert(run.FrameResults == 3U);
    assert(run.StereoFrames == (3U * 1152U));
    test_free_blob(&blob);
}

/**
 * @brief 窗口只接收剩余空间；满了之后继续交入返回 0；结束输入后拒绝再交。
 */
static void test_feed_reports_accepted_bytes_and_rejects_after_finish(void)
{
    static uint8_t filler[PLAYBACK_SERVICE_WINDOW_BYTES + 1000U];
    uint32_t accepted = 0U;

    memset(filler, 0, sizeof(filler));
    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_BeginStream(0U) == SERVICE_OK);

    assert(Service_Playback_Feed(filler, (uint32_t)sizeof(filler), &accepted) == SERVICE_OK);
    assert(accepted == PLAYBACK_SERVICE_WINDOW_BYTES);
    assert(Service_Playback_Feed(filler, 10U, &accepted) == SERVICE_OK);
    assert(accepted == 0U);

    assert(Service_Playback_FinishInput() == SERVICE_OK);
    assert(Service_Playback_Feed(filler, 10U, &accepted) == SERVICE_NOT_READY);
    assert(accepted == 0U);
}

/**
 * @brief 开始新流会丢弃窗口里没解完的旧字节，新流从干净窗口起步。
 */
static void test_begin_stream_discards_previous_window_content(void)
{
    test_blob_t blob;
    test_run_t run;
    uint32_t accepted = 0U;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    assert(Service_Playback_Init() == SERVICE_OK);
    assert(Service_Playback_BeginStream(0U) == SERVICE_OK);
    assert(Service_Playback_Feed(blob.Data + 5U, 3000U, &accepted) == SERVICE_OK); /* 偏 5 字节的残片 */
    assert(accepted == 3000U);

    test_run_stream(blob.Data, blob.Size, (uint32_t)blob.Size, 0U, &run); /* 内部再次 Init + BeginStream */
    assert(run.FrameResults == CBR_AUDIO_FRAMES);
    assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
    test_free_blob(&blob);
}

/**
 * @brief 流尾被截断的残余静默丢弃：完整帧全部解出，以 END 收场，不报同步预算耗尽。
 * @note  分别在末帧中间、末帧只剩一个帧头处截断。残余不足 4 字节时 minimp3 无法确认前一帧之后
 *        还有帧头，会连带丢掉前一帧（#46 的不足即丢弃），属于正常播完前的流尾损失，不在此断言。
 */
static void test_truncated_tail_is_silently_dropped(void)
{
    test_blob_t blob;
    const uint32_t cuts[] = {200U, 4U};
    size_t i;
    size_t last_frame_start;

    test_load_fixture("cbr_44100_stereo.mp3", &blob);
    last_frame_start = test_frame_end_offset(blob.Data, 0U, CBR_AUDIO_FRAMES - 1U);
    assert(test_frame_end_offset(blob.Data, last_frame_start, 1U) == blob.Size);

    for (i = 0U; i < (sizeof(cuts) / sizeof(cuts[0])); i++)
    {
        test_run_t run;
        const size_t truncated = last_frame_start + cuts[i];

        test_run_stream(blob.Data, truncated, (uint32_t)truncated, 777U, &run);

        assert(run.FinalResult == SERVICE_PLAYBACK_ADVANCE_END);
        assert(run.FrameResults == (CBR_AUDIO_FRAMES - 1U));
        assert(run.ReadFrameFailures == 0);
    }
    test_free_blob(&blob);
}

/**
 * @brief 测试入口。
 * @return 0 表示全部通过；断言失败会直接终止进程。
 */
int main(void)
{
    test_rejects_calls_before_init_and_without_stream();
    test_every_fixture_decodes_all_frames_for_any_chunking();
    test_waits_for_window_minimum_before_decoding();
    test_decodes_short_remainder_after_input_ended();
    test_feed_reports_accepted_bytes_and_rejects_after_finish();
    test_begin_stream_discards_previous_window_content();
    test_truncated_tail_is_silently_dropped();
    (void)puts("playback_service_stream: OK");
    return 0;
}
