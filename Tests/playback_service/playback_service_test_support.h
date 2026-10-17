/**
 * @file playback_service_test_support.h
 * @brief 播放模块主机测试的共用夹具读取与整流驱动。
 * @note  只经 playback_service.h 的公开接口驱动被测模块；这里的辅助函数属于测试，
 *        不向生产头增加任何入口。
 */

#ifndef PLAYBACK_SERVICE_TEST_SUPPORT_H
#define PLAYBACK_SERVICE_TEST_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include "Service/playback/playback_service.h"

/** @brief 一个夹具文件的内存副本。 */
typedef struct
{
    uint8_t *Data;
    size_t Size;
} test_blob_t;

/** @brief 一次整流驱动的观察结果。 */
typedef struct
{
    uint32_t FrameResults;          /**< 回报 FRAME 的次数。 */
    uint32_t SkippedResults;        /**< 回报 SKIPPED 的次数。 */
    uint32_t NeedInputResults;      /**< 回报 NEED_INPUT 的次数。 */
    uint32_t FirstFrameReports;     /**< FirstFrame 为真的报告次数。 */
    uint32_t FirstFrameAtResult;    /**< 回报首帧那次推进的 Result。 */
    uint32_t SkippedBeforeFirst;    /**< 首帧之前的 SKIPPED 次数（元数据帧等）。 */
    uint32_t StereoFrames;          /**< 取走的立体声帧总数（每声道采样数之和）。 */
    uint32_t Frames1152;            /**< PcmFrames 为 1152 的帧数。 */
    uint32_t Frames576;             /**< PcmFrames 为 576 的帧数。 */
    uint64_t PcmHash;               /**< 取走的全部立体声 PCM 的 FNV-1a 64 位哈希。 */
    int PeakAbs;                    /**< 取走的全部采样的最大绝对值。 */
    uint32_t LeftRightDiffer;       /**< 左右声道不相等的立体声帧数（单声道流应为 0，立体声夹具应大于 0）。 */
    int TimeWentBackwards;          /**< DecodedMs 回退的次数（应为 0）。 */
    uint32_t LastDecodedMs;         /**< 最后一次报告的已解码时间。 */
    uint32_t MaxDecodedMs;          /**< 全程报告的最大已解码时间。 */
    int ReadFrameFailures;          /**< FRAME 之后 ReadFrame 失败的次数（应为 0）。 */
    Service_Playback_AdvanceResultTypeDef FinalResult; /**< 终止循环的那次 Result（END 或 SYNC_EXHAUSTED）。 */
    Service_Playback_StreamInfoTypeDef StreamInfo;     /**< 首帧信息。 */
} test_run_t;

void test_load_fixture(const char *name, test_blob_t *blob);
void test_free_blob(test_blob_t *blob);

/**
 * 把 data 按 chunk 字节一块块交入并推进，直到 END 或 SYNC_EXHAUSTED。
 * data 全部交完后自动 FinishInput。chunk 为 0 表示一次尽量多交。
 */
void test_run_stream(const uint8_t *data, size_t size, uint32_t audio_bytes, uint32_t chunk, test_run_t *run);

/** 沿 MPEG-1/2 Layer III 帧头走 count 帧，返回第 count 帧末尾的字节偏移。 */
size_t test_frame_end_offset(const uint8_t *data, size_t offset, uint32_t count);

/** @brief 解码出的立体声 PCM（交错 int16），用解码半边的 Advance/ReadFrame 得到，作为输出管线的独立期望。 */
typedef struct
{
    int16_t *Pcm;
    size_t Frames; /**< 立体声帧数。 */
} test_pcm_t;

/** @brief 按块喂入码流的喂入器：模拟播放任务把块字节交给播放模块并在读尽时告知。 */
typedef struct
{
    const uint8_t *Data;
    size_t Size;
    size_t Offset;
    uint32_t Chunk;      /**< 每次尝试交入的字节数；0 表示尽量多。 */
    int Finished;        /**< 已调用 FinishInput。 */
    uint32_t Limit;      /**< 非 0 时最多交入这么多字节（模拟后面的块还没到），且不告知读尽。 */
} test_feeder_t;

/** @brief 驱动输出管线时累计的事实。 */
typedef struct
{
    uint32_t Pumps;
    uint32_t Started;           /**< OutputStarted 次数。 */
    uint32_t Progress;          /**< OutputProgress 次数。 */
    uint32_t Ended;             /**< StreamEnded 次数。 */
    uint32_t FirstFrames;       /**< FirstFrame 次数。 */
    uint32_t NeedInputs;
    uint32_t Underruns;         /**< Underruns 字段之和。 */
    uint32_t SyncExhausted;
    uint32_t StartRejected;
    uint32_t TransferErrors;
    uint32_t StopFailed;
    uint32_t TimeWentBackwards;
    uint32_t LastDecodedMs;
    Service_Playback_StreamInfoTypeDef Info;
} test_facts_t;

void test_decode_reference(const uint8_t *data, size_t size, uint32_t audio_bytes, test_pcm_t *out);
void test_free_pcm(test_pcm_t *pcm);

void test_feeder_init(test_feeder_t *feeder, const uint8_t *data, size_t size, uint32_t chunk);
void test_feeder_feed(test_feeder_t *feeder);

/** 先喂入再 Pump 一次，并把报告并入 facts；返回 Pump 的状态码（SERVICE_OK 时才并入）。 */
Service_StatusTypeDef test_pump(test_feeder_t *feeder, test_facts_t *facts, Service_Playback_PumpReportTypeDef *report);

/**
 * 模拟任务反复“喂入 + Pump + DMA 释放一个半区”，直到报告 StreamEnded 或轮数用尽。
 * 返回是否因 StreamEnded 结束。
 */
int test_play_until_ended(test_feeder_t *feeder, test_facts_t *facts, uint32_t max_rounds);

/** 复位假 Platform，Init 播放模块并开始一条新流，喂入器指向 blob 的前 size 字节。 */
void test_begin_stream(const test_blob_t *blob, size_t size, uint32_t chunk, test_feeder_t *feeder);

/** 断言播放日志按序等于参考 PCM，其后（若有）到 played_frames 为止全是静音。 */
void test_assert_played_equals_reference_then_silence(const test_pcm_t *ref, size_t played_frames);

#endif /* PLAYBACK_SERVICE_TEST_SUPPORT_H */
