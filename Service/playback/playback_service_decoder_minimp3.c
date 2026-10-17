/**
  ******************************************************************************
  * @file    playback_service_decoder_minimp3.c
  * @brief   playback_service_decoder.h 的 minimp3 实现。
  ******************************************************************************
  */

#include "Service/playback/playback_service_decoder.h"
#include "Service/playback/playback_service_config.h"

#include <string.h>

/*
 * 固定编译开关，使主机测试与固件走同一份标量代码：
 * - MINIMP3_NO_SIMD：固件为 Cortex-M7 无 NEON，主机测试在 x86 上也不走 SSE；
 * - MINIMP3_ONLY_MP3：0.6.0 只支持 Layer III（ADR-0017），去掉 Layer I/II 解码路径。
 * 输出为默认 16 位 PCM（ADR-0017 第 4 条），只用低层 mp3dec_decode_frame。
 */
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION

/* 第三方代码不修改（ADR-0017 第 2 条）；其告警只在包含处局部压制。 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "Middlewares/Third_Party/minimp3/minimp3.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

_Static_assert(MINIMP3_MAX_SAMPLES_PER_FRAME == (PLAYBACK_SERVICE_FRAME_MAX_SAMPLES * 2U),
               "帧输出缓冲容量必须与 minimp3 单帧最大输出一致");
_Static_assert(sizeof(mp3d_sample_t) == sizeof(int16_t), "解码输出必须是 16 位 PCM");

/** @brief 解码器状态，静态位于 DTCM（ADR-0017 第 3 条）；每帧 scratch 是 mp3dec_decode_frame 的栈上局部变量。 */
static mp3dec_t s_decoder;

/**
 * @brief 将解码器置于初始状态，丢弃跨帧的位储备与重叠相加状态。
 * @note  开始新流、换曲或失步重同步后调用。不分配内存。
 */
void playback_service_decoder_reset(void)
{
    mp3dec_init(&s_decoder);
}

/**
 * @brief 在输入头部寻找并解码至多一帧。
 * @param[in]  input       线性输入窗口头部；解码器不保留该指针，也不修改内容。
 * @param[in]  input_bytes 窗口内有效字节数；调用方保证不小于 W_min，输入已结束时除外。
 * @param[out] pcm         帧输出缓冲，至少 2 × PLAYBACK_SERVICE_FRAME_MAX_SAMPLES 个 int16_t；
 *                         立体声按左右交错，单声道每个采样一个值。
 * @param[out] info        本次消耗的字节数与帧信息；找不到帧时 SampleRateHz 为 0。
 * @return 解出的每声道采样数；0 表示没有可用 PCM（找不到帧、帧损坏、位储备不足，或非 Layer III）。
 * @note   ConsumedBytes 在返回 0 时同样推进：找不到帧时 minimp3 丢弃整个窗口（#46），
 *         调用方须按 ConsumedBytes 推进窗口，否则会原地空转。
 *         栈占用约 17 KB（mp3dec_scratch_t），见 ADR-0019。
 */
uint32_t playback_service_decoder_decode(
    const uint8_t *input,
    uint32_t input_bytes,
    int16_t *pcm,
    playback_service_decoder_info_t *info)
{
    mp3dec_frame_info_t frame_info;
    int samples;

    memset(&frame_info, 0, sizeof(frame_info));
    samples = mp3dec_decode_frame(&s_decoder, input, (int)input_bytes, pcm, &frame_info);

    info->ConsumedBytes = (frame_info.frame_bytes > 0) ? (uint32_t)frame_info.frame_bytes : 0U;
    if (info->ConsumedBytes > input_bytes)
    {
        info->ConsumedBytes = input_bytes;
    }
    if ((info->ConsumedBytes == 0U) && (input_bytes > 0U))
    {
        info->ConsumedBytes = 1U; /* 保证推进，避免异常返回值造成死循环。 */
    }

    if (frame_info.hz > 0)
    {
        info->FrameOffset = (uint32_t)frame_info.frame_offset;
        info->SampleRateHz = (uint32_t)frame_info.hz;
        info->BitrateKbps = (uint32_t)frame_info.bitrate_kbps;
        info->Channels = (uint8_t)frame_info.channels;
    }
    else
    {
        info->FrameOffset = 0U;
        info->SampleRateHz = 0U;
        info->BitrateKbps = 0U;
        info->Channels = 0U;
    }

    return (samples > 0) ? (uint32_t)samples : 0U;
}
