/**
  ******************************************************************************
  * @file    playback_service_decoder.h
  * @brief   播放模块私有解码接口：音频帧字节 → PCM 帧与帧信息。
  ******************************************************************************
  */

#ifndef PLAYBACK_SERVICE_DECODER_H
#define PLAYBACK_SERVICE_DECODER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 一次解码调用回报的帧信息。
 * @note  解码库的类型（如 minimp3 的 mp3dec_frame_info_t）不出现在本头中，换库只改
 *        playback_service_decoder_minimp3.c（ADR-0017 第 5 条）。
 */
typedef struct
{
    uint32_t ConsumedBytes;   /**< 本次从输入头部消耗的字节数，含帧前跳过的字节；输入非空时恒不小于 1。 */
    uint32_t FrameOffset;     /**< 帧头相对输入起点的偏移；仅找到帧（SampleRateHz 非 0）时有效。 */
    uint32_t SampleRateHz;    /**< 找到的帧的采样率；没有找到帧时为 0。 */
    uint32_t BitrateKbps;     /**< 找到的帧的标称比特率；自由格式时可能为 0。 */
    uint8_t  Channels;        /**< 找到的帧的声道数（1 或 2）；没有找到帧时为 0。 */
} playback_service_decoder_info_t;

/**
 * @brief 首帧内的流头（Xing/Info 或 VBRI）解析结果。
 */
typedef struct
{
    bool     IsMetadataFrame;  /**< 首帧是 Xing/Info 或 VBRI 元数据帧，本身不含可听音频，须跳过。 */
    bool     HasTotalFrames;   /**< 流头带音频总帧数。 */
    uint32_t TotalFrames;      /**< 音频总帧数，不含元数据帧本身；HasTotalFrames 为真时有效。 */
    uint32_t SamplesPerFrame;  /**< 该帧头对应的每声道采样数（MPEG-1 为 1152，MPEG-2/2.5 为 576）。 */
} playback_service_decoder_stream_header_t;

void playback_service_decoder_reset(void);
uint32_t playback_service_decoder_decode(
    const uint8_t *input,
    uint32_t input_bytes,
    int16_t *pcm,
    playback_service_decoder_info_t *info);
void playback_service_decoder_probe_stream_header(
    const uint8_t *frame,
    uint32_t frame_bytes,
    playback_service_decoder_stream_header_t *header);

#endif /* PLAYBACK_SERVICE_DECODER_H */
