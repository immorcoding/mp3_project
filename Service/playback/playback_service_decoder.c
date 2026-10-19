/**
  ******************************************************************************
  * @file    playback_service_decoder.c
  * @brief   与解码库无关的流头解析：Xing/Info 与 VBRI 元数据帧。
  ******************************************************************************
  */

#include "Service/playback/playback_service_decoder.h"

#include <string.h>

/* MPEG 音频帧头（4 字节）的字段位置。 */
#define MP3_HEADER_BYTES            (4U)
#define MP3_VERSION_MPEG1_MASK      (0x08U)  /* 帧头字节 1：置位为 MPEG-1，清零为 MPEG-2/2.5。 */
#define MP3_CHANNEL_MODE_MASK       (0xC0U)  /* 帧头字节 3：声道模式。 */
#define MP3_CHANNEL_MODE_MONO       (0xC0U)

/* Xing/Info 标签相对帧起点的偏移 = 帧头 4 字节 + 边信息长度。 */
#define XING_OFFSET_MPEG1_STEREO    (36U)
#define XING_OFFSET_MPEG1_MONO      (21U)
#define XING_OFFSET_MPEG2_STEREO    (21U)
#define XING_OFFSET_MPEG2_MONO      (13U)
#define XING_FLAG_FRAMES            (0x00000001UL)
#define XING_TAG_AND_FLAGS_BYTES    (8U)
#define XING_FRAMES_FIELD_BYTES     (4U)

/* VBRI 标签固定位于帧头后 32 字节处；版本 2、延迟 2、质量 2、字节数 4 之后是 4 字节帧数。 */
#define VBRI_OFFSET                 (36U)
#define VBRI_FRAMES_OFFSET          (VBRI_OFFSET + 4U + 2U + 2U + 2U + 4U)

/**
 * @brief  读取大端 32 位整数。
 * @param[in] bytes 指向至少 4 字节的数据。
 * @return 解出的数值。
 */
static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

/**
 * @brief 判断首帧是否为 Xing/Info 或 VBRI 元数据帧，并取出音频总帧数与每帧采样数。
 * @param[in]  frame       首帧起点（帧头第一字节），至少 frame_bytes 字节可读。
 * @param[in]  frame_bytes 首帧字节数。
 * @param[out] header      解析结果；frame_bytes 不足一个帧头时只写出默认值。
 * @note   只看这一帧，不做任何推进；调用方只对一条流的第一帧调用。minimp3 把这类元数据帧
 *         当作静音帧解码（#46），因此必须在上层识别并跳过。
 */
void playback_service_decoder_probe_stream_header(
    const uint8_t *frame,
    uint32_t frame_bytes,
    playback_service_decoder_stream_header_t *header)
{
    bool mpeg1;
    bool mono;
    uint32_t xing_offset;

    header->IsMetadataFrame = false;
    header->HasTotalFrames = false;
    header->TotalFrames = 0U;
    header->SamplesPerFrame = 1152U;

    if (frame_bytes < MP3_HEADER_BYTES)
    {
        return;
    }

    mpeg1 = ((frame[1] & MP3_VERSION_MPEG1_MASK) != 0U);
    mono = ((frame[3] & MP3_CHANNEL_MODE_MASK) == MP3_CHANNEL_MODE_MONO);
    header->SamplesPerFrame = mpeg1 ? 1152U : 576U;

    if (mpeg1)
    {
        xing_offset = mono ? XING_OFFSET_MPEG1_MONO : XING_OFFSET_MPEG1_STEREO;
    }
    else
    {
        xing_offset = mono ? XING_OFFSET_MPEG2_MONO : XING_OFFSET_MPEG2_STEREO;
    }

    if ((frame_bytes >= (xing_offset + XING_TAG_AND_FLAGS_BYTES)) &&
        ((memcmp(frame + xing_offset, "Xing", 4U) == 0) || (memcmp(frame + xing_offset, "Info", 4U) == 0)))
    {
        const uint32_t flags = read_be32(frame + xing_offset + 4U);

        header->IsMetadataFrame = true;
        if (((flags & XING_FLAG_FRAMES) != 0U) &&
            (frame_bytes >= (xing_offset + XING_TAG_AND_FLAGS_BYTES + XING_FRAMES_FIELD_BYTES)))
        {
            header->HasTotalFrames = true;
            header->TotalFrames = read_be32(frame + xing_offset + XING_TAG_AND_FLAGS_BYTES);
        }
        return;
    }

    if ((frame_bytes >= (VBRI_FRAMES_OFFSET + 4U)) && (memcmp(frame + VBRI_OFFSET, "VBRI", 4U) == 0))
    {
        header->IsMetadataFrame = true;
        header->HasTotalFrames = true;
        header->TotalFrames = read_be32(frame + VBRI_FRAMES_OFFSET);
    }
}
