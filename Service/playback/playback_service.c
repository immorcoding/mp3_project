/**
  ******************************************************************************
  * @file    playback_service.c
  * @brief   播放模块：线性输入窗口、同步预算与首帧信息（解码半边）。
  ******************************************************************************
  */

#include "Service/playback/playback_service.h"
#include "Service/playback/playback_service_decoder.h"

#include <string.h>

_Static_assert(PLAYBACK_SERVICE_WINDOW_MIN_BYTES >= 2308U, "W_min 不得低于 2308 B（minimp3 流式输入合同，#46）");
_Static_assert(PLAYBACK_SERVICE_WINDOW_BYTES >= PLAYBACK_SERVICE_WINDOW_MIN_BYTES, "线性输入窗口必须容纳 W_min");
_Static_assert(PLAYBACK_SERVICE_SYNC_BUDGET_BYTES >= PLAYBACK_SERVICE_WINDOW_BYTES, "同步预算 R 不得小于一个窗口，否则一次整窗丢弃就会耗尽");

/** @brief 一条流的运行状态；BeginStream() 整体重置。 */
typedef struct
{
    bool     InputEnded;           /**< 播放任务已告知输入读尽。 */
    bool     SyncExhausted;        /**< 同步预算 R 已耗尽。 */
    bool     HeaderProbed;         /**< 已对本流第一个解出的帧做过 Xing/VBRI 检查。 */
    bool     FirstFrameDecoded;    /**< 已解出本流首帧，采样率已定。 */
    uint32_t AudioBytes;           /**< 开启 OK 带回的音频区间字节数，用于估算总时长。 */
    uint32_t UnsyncedBytes;        /**< 自上一个好帧起连续解不出帧的字节数（R 的计数）。 */
    uint64_t DecodedSamples;       /**< 已解出的每声道采样数。 */
    bool     HeaderHasFrames;      /**< 流头带音频总帧数。 */
    uint32_t HeaderTotalFrames;    /**< 流头给出的音频总帧数。 */
    uint32_t HeaderSamplesPerFrame; /**< 流头所属帧的每声道采样数。 */
    Service_Playback_StreamInfoTypeDef Info; /**< 首帧信息。 */
} playback_service_stream_t;

static bool s_initialized;
static bool s_stream_active;
static playback_service_stream_t s_stream;

/** @brief 线性输入窗口，静态位于 DTCM；解码器只读它，不直接读音频块。 */
static uint8_t s_window[PLAYBACK_SERVICE_WINDOW_BYTES];
static uint32_t s_window_fill;

/** @brief 帧输出缓冲，静态位于 DTCM：minimp3 的单帧输出，立体声交错、单声道单值（4608 B）。 */
static int16_t s_frame_pcm[PLAYBACK_SERVICE_FRAME_MAX_SAMPLES * 2U];
static uint32_t s_frame_samples;
static uint8_t s_frame_channels;
static bool s_frame_pending;

/**
 * @brief  把已解码样本数换算为毫秒。
 * @param[in] samples        每声道采样数。
 * @param[in] sample_rate_hz 采样率；为 0 时返回 0。
 * @return 向下取整的毫秒数，饱和在 UINT32_MAX。
 */
static uint32_t samples_to_ms(uint64_t samples, uint32_t sample_rate_hz)
{
    uint64_t ms;

    if (sample_rate_hz == 0U)
    {
        return 0U;
    }

    ms = (samples * 1000ULL) / sample_rate_hz;
    return (ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)ms;
}

/**
 * @brief  当前已解码时间；越过总时长时钳在总时长（D5）。
 * @return 毫秒数。
 */
static uint32_t decoded_ms(void)
{
    const uint32_t ms = samples_to_ms(s_stream.DecodedSamples, s_stream.Info.SampleRateHz);

    if ((s_stream.Info.TotalDurationMs > 0U) && (ms > s_stream.Info.TotalDurationMs))
    {
        return s_stream.Info.TotalDurationMs;
    }
    return ms;
}

/**
 * @brief 首帧解出时确定总时长：Xing/VBRI 的帧数优先，缺失时按音频字节数与首帧比特率估算。
 * @param[in] bitrate_kbps 首个音频帧的标称比特率。
 * @note  估算不修正：VBR 文件缺少流头时估算可能偏离实际，这是已接受的取舍（#54）。
 */
static void resolve_total_duration(uint32_t bitrate_kbps)
{
    if (s_stream.HeaderHasFrames && (s_stream.HeaderTotalFrames > 0U))
    {
        const uint64_t samples = (uint64_t)s_stream.HeaderTotalFrames * s_stream.HeaderSamplesPerFrame;

        s_stream.Info.TotalDurationMs = samples_to_ms(samples, s_stream.Info.SampleRateHz);
        s_stream.Info.DurationFromHeader = true;
    }
    else if ((s_stream.AudioBytes > 0U) && (bitrate_kbps > 0U))
    {
        const uint64_t ms = ((uint64_t)s_stream.AudioBytes * 8ULL) / bitrate_kbps;

        s_stream.Info.TotalDurationMs = (ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)ms;
        s_stream.Info.DurationFromHeader = false;
    }
    else
    {
        s_stream.Info.TotalDurationMs = 0U;
        s_stream.Info.DurationFromHeader = false;
    }
}

/**
 * @brief 把窗口头部 count 字节推进掉，余量前移（线性窗口，每次按 frame_bytes 推进）。
 * @param[in] count 要丢弃的字节数，不超过 s_window_fill。
 */
static void window_consume(uint32_t count)
{
    s_window_fill -= count;
    if (s_window_fill > 0U)
    {
        memmove(s_window, s_window + count, s_window_fill);
    }
}

/**
 * @brief 记入一段解不出帧的字节；输入未结束时累计到 R 即判预算耗尽。
 * @param[in] bytes 本次未能解出帧的字节数。
 * @note  输入已结束后的残余是流尾截断，按 B13 静默丢弃，不受 R 影响。
 */
static void count_unsynced(uint32_t bytes)
{
    if (s_stream.InputEnded)
    {
        return;
    }

    s_stream.UnsyncedBytes += bytes;
    if (s_stream.UnsyncedBytes >= PLAYBACK_SERVICE_SYNC_BUDGET_BYTES)
    {
        s_stream.SyncExhausted = true;
    }
}

/**
 * @brief 初始化播放模块的静态状态。
 * @return SERVICE_OK。可重复调用；调用后当前流作废，需重新 BeginStream()。
 * @note   不分配内存，不访问硬件；任务上下文调用。
 */
Service_StatusTypeDef Service_Playback_Init(void)
{
    memset(&s_stream, 0, sizeof(s_stream));
    s_stream_active = false;
    s_window_fill = 0U;
    s_frame_samples = 0U;
    s_frame_channels = 0U;
    s_frame_pending = false;
    playback_service_decoder_reset();
    s_initialized = true;
    return SERVICE_OK;
}

/**
 * @brief 开始新流：重置解码器、线性输入窗口、已解码时间与同步预算。
 * @param[in] audio_bytes 音频区间字节数（开启 OK 带回），仅用于流头缺失时估算总时长；0 表示未知。
 * @return SERVICE_OK；未 Init 返回 SERVICE_NOT_READY。
 * @note   不认识曲目或流序号；换曲、重选同一首都调用本函数。
 */
Service_StatusTypeDef Service_Playback_BeginStream(uint32_t audio_bytes)
{
    if (!s_initialized)
    {
        return SERVICE_NOT_READY;
    }

    memset(&s_stream, 0, sizeof(s_stream));
    s_stream.AudioBytes = audio_bytes;
    s_window_fill = 0U;
    s_frame_pending = false;
    playback_service_decoder_reset();
    s_stream_active = true;
    return SERVICE_OK;
}

/**
 * @brief 把音频帧流字节拷入线性输入窗口。
 * @param[in]  data     输入字节；调用返回后即可复用，播放模块不保留指针。
 * @param[in]  length   输入字节数。
 * @param[out] accepted 实际接收的字节数（窗口剩余空间不足时少于 length，可为 0）；调用方对余量
 *                      稍后重交。
 * @return SERVICE_OK；参数为空返回 SERVICE_INVALID_PARAM；无活动流或输入已结束返回 SERVICE_NOT_READY。
 */
Service_StatusTypeDef Service_Playback_Feed(const uint8_t *data, uint32_t length, uint32_t *accepted)
{
    uint32_t room;
    uint32_t count;

    if (accepted == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    *accepted = 0U;

    if ((data == NULL) && (length > 0U))
    {
        return SERVICE_INVALID_PARAM;
    }
    if (!s_stream_active || s_stream.InputEnded)
    {
        return SERVICE_NOT_READY;
    }

    room = PLAYBACK_SERVICE_WINDOW_BYTES - s_window_fill;
    count = (length < room) ? length : room;
    if (count > 0U)
    {
        memcpy(s_window + s_window_fill, data, count);
        s_window_fill += count;
    }

    *accepted = count;
    return SERVICE_OK;
}

/**
 * @brief 标记本流输入已读尽：此后窗口内的余量原样交给解码器直到耗尽。
 * @return SERVICE_OK；无活动流返回 SERVICE_NOT_READY。
 */
Service_StatusTypeDef Service_Playback_FinishInput(void)
{
    if (!s_stream_active)
    {
        return SERVICE_NOT_READY;
    }

    s_stream.InputEnded = true;
    return SERVICE_OK;
}

/**
 * @brief 推进解码：最多对窗口头部做一次解码调用，并回报事实。
 * @param[out] report 本次推进的结果与事实；返回非 SERVICE_OK 时不写。
 * @return SERVICE_OK；参数为空返回 SERVICE_INVALID_PARAM；无活动流返回 SERVICE_NOT_READY。
 * @note   解码门槛（D3）：窗口字节数低于 W_min 且输入未结束时不调用解码，回报 NEED_INPUT。
 *         同步预算（D4）：自音频区间开头或上一个好帧起连续 R 字节解不出帧回报 SYNC_EXHAUSTED；
 *         与首帧采样率不同的帧丢弃（不出 PCM、不计入已解码时间）并计入 R。
 *         首帧若是 Xing/Info 或 VBRI 元数据帧则跳过它，首帧信息在其后的第一个音频帧上回报。
 *         上一帧未被 ReadFrame() 取走时，本次推进将其丢弃。
 *         阻塞性：纯计算，约一帧解码耗时（板上耗时待实测）；栈占用见解码实现。
 */
Service_StatusTypeDef Service_Playback_Advance(Service_Playback_AdvanceReportTypeDef *report)
{
    playback_service_decoder_info_t info;
    uint32_t samples;
    uint32_t consumed;

    if (report == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    if (!s_stream_active)
    {
        return SERVICE_NOT_READY;
    }

    memset(report, 0, sizeof(*report));
    report->StreamInfo = s_stream.Info;
    s_frame_pending = false;

    if (s_stream.SyncExhausted)
    {
        report->Result = SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED;
        report->DecodedMs = decoded_ms();
        return SERVICE_OK;
    }

    if ((s_window_fill < PLAYBACK_SERVICE_WINDOW_MIN_BYTES) && !s_stream.InputEnded)
    {
        report->Result = SERVICE_PLAYBACK_ADVANCE_NEED_INPUT;
        report->DecodedMs = decoded_ms();
        return SERVICE_OK;
    }
    if (s_window_fill == 0U)
    {
        report->Result = SERVICE_PLAYBACK_ADVANCE_END;
        report->DecodedMs = decoded_ms();
        return SERVICE_OK;
    }

    samples = playback_service_decoder_decode(s_window, s_window_fill, s_frame_pcm, &info);
    consumed = info.ConsumedBytes;

    if (samples == 0U)
    {
        window_consume(consumed);
        count_unsynced(consumed);
        report->Result = s_stream.SyncExhausted ? SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED : SERVICE_PLAYBACK_ADVANCE_SKIPPED;
        report->DecodedMs = decoded_ms();
        return SERVICE_OK;
    }

    if (!s_stream.HeaderProbed)
    {
        playback_service_decoder_stream_header_t header;

        s_stream.HeaderProbed = true;
        playback_service_decoder_probe_stream_header(s_window + info.FrameOffset, consumed - info.FrameOffset, &header);
        if (header.IsMetadataFrame)
        {
            s_stream.HeaderHasFrames = header.HasTotalFrames;
            s_stream.HeaderTotalFrames = header.TotalFrames;
            s_stream.HeaderSamplesPerFrame = header.SamplesPerFrame;
            window_consume(consumed);
            s_stream.UnsyncedBytes = 0U;
            report->Result = SERVICE_PLAYBACK_ADVANCE_SKIPPED;
            report->DecodedMs = decoded_ms();
            return SERVICE_OK;
        }
    }

    window_consume(consumed);

    if (s_stream.FirstFrameDecoded && (info.SampleRateHz != s_stream.Info.SampleRateHz))
    {
        count_unsynced(consumed);
        report->Result = s_stream.SyncExhausted ? SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED : SERVICE_PLAYBACK_ADVANCE_SKIPPED;
        report->DecodedMs = decoded_ms();
        return SERVICE_OK;
    }

    s_stream.UnsyncedBytes = 0U;

    if (!s_stream.FirstFrameDecoded)
    {
        s_stream.FirstFrameDecoded = true;
        s_stream.Info.SampleRateHz = info.SampleRateHz;
        s_stream.Info.Channels = info.Channels;
        resolve_total_duration(info.BitrateKbps);
        report->FirstFrame = true;
        report->StreamInfo = s_stream.Info;
    }

    s_stream.DecodedSamples += samples;
    s_frame_samples = samples;
    s_frame_channels = info.Channels;
    s_frame_pending = true;

    report->Result = SERVICE_PLAYBACK_ADVANCE_FRAME;
    report->PcmFrames = samples;
    report->DecodedMs = decoded_ms();
    return SERVICE_OK;
}

/**
 * @brief 把帧输出缓冲里的一帧 PCM 拷成立体声交错；单声道在此复制到左右声道。
 * @param[out] stereo_pcm      目标缓冲，容纳 capacity_frames 个立体声帧（每帧 2 个 int16_t）。
 * @param[in]  capacity_frames 目标缓冲的立体声帧容量。
 * @param[out] frames          实际写入的立体声帧数。
 * @return SERVICE_OK；参数为空返回 SERVICE_INVALID_PARAM；没有待取的帧返回 SERVICE_NOT_READY；
 *         容量不足返回 SERVICE_NO_SPACE（帧保留，可换更大的缓冲重取）。
 * @note   一帧只能取走一次；下一次 Advance() 会丢弃未取走的帧。
 */
Service_StatusTypeDef Service_Playback_ReadFrame(int16_t *stereo_pcm, uint32_t capacity_frames, uint32_t *frames)
{
    uint32_t i;

    if ((stereo_pcm == NULL) || (frames == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }
    *frames = 0U;

    if (!s_frame_pending)
    {
        return SERVICE_NOT_READY;
    }
    if (capacity_frames < s_frame_samples)
    {
        return SERVICE_NO_SPACE;
    }

    if (s_frame_channels == 1U)
    {
        for (i = 0U; i < s_frame_samples; i++)
        {
            stereo_pcm[2U * i] = s_frame_pcm[i];
            stereo_pcm[(2U * i) + 1U] = s_frame_pcm[i];
        }
    }
    else
    {
        memcpy(stereo_pcm, s_frame_pcm, (size_t)s_frame_samples * 2U * sizeof(int16_t));
    }

    *frames = s_frame_samples;
    s_frame_pending = false;
    return SERVICE_OK;
}
