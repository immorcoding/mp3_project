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

#endif /* PLAYBACK_SERVICE_TEST_SUPPORT_H */
