/**
  ******************************************************************************
  * @file    playback_service.c
  * @brief   播放模块：线性输入窗口、同步预算与首帧信息（解码半边）。
  ******************************************************************************
  */

#include "Service/playback/playback_service.h"
#include "Service/playback/playback_service_decoder.h"
#include "Platform/audio/platform_audio.h"

#include <string.h>

#define PLAYBACK_SERVICE_PCM_HALF_COUNT   (2U)
#define PLAYBACK_SERVICE_PCM_HALF_SAMPLES (PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF * 2U)

_Static_assert((PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF % 1152U) == 0U, "每半区帧数须为 1152 的整数倍，使整帧不跨半区（D7）");
_Static_assert(((PLAYBACK_SERVICE_PCM_HALF_SAMPLES * sizeof(int16_t)) % PLATFORM_DCACHE_LINE_SIZE) == 0U, "半区字节数须按 D-Cache 行对齐（Platform_Audio_Start 合同）");
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
static uint32_t s_frame_offset; /**< 输出管线已从帧输出缓冲取走的每声道采样数；只有整帧不能放进一个半区时才非 0。 */

/** @brief 半区内容的种类；随半区内容一起交换（同曲恢复，D9）。 */
typedef enum
{
    PLAYBACK_SERVICE_HALF_DATA = 0U, /**< 流中的真实音频（整半区）。 */
    PLAYBACK_SERVICE_HALF_LAST,      /**< 含最后一段真实音频的半区，不满的部分已补 0。 */
    PLAYBACK_SERVICE_HALF_SILENCE    /**< 流尾之后填的静音。 */
} playback_service_half_kind_t;

/**
 * @brief 输出管线状态；BeginStream() 与 AbandonStream() 整体重置（STOP_FAILED 锁存除外）。
 * @note  半区按“补满序号”编号：第 j 次补满（从 0 起）写半区 j % 2，并在 Platform 的第 j + 1 次释放时被读完。
 *        Start 之前补 j = 0、1 两个半区；此后每次释放补一个，所以运行中 Fills 恒等于“已处理的释放数 + 2”。
 */
typedef struct
{
    bool     Running;       /**< Platform 输出正在运行（Start 成功且尚未 Stop）。 */
    bool     Paused;        /**< 用户意图暂停：只补满，不 Start。 */
    bool     Faulted;       /**< Platform 锁存了 STOP_FAILED，模块不再写 PCM 半区；只有 Init 解除。 */
    bool     Rejected;      /**< Platform 因参数拒绝了 Start，本流不可播。 */
    bool     EndedPending;  /**< 流尾已放完（或补满时发现没有可放的音频），StreamEnded 事实持续给出。 */
    bool     DecodeEnded;   /**< 解码已读尽：输入读尽且窗口与帧缓冲都已耗尽。 */
    uint32_t Fills;         /**< 已完成的补满次数。 */
    uint32_t FillFrames;    /**< 正在补的半区已写入的立体声帧数。 */
    uint32_t Released;      /**< 已知的 Platform 释放数；暂停与恢复之间冻结。 */
    uint32_t SeenUnderruns; /**< 已处理的 Platform 欠载数。 */
    uint32_t LastRelease;   /**< 最后一个含真实音频的半区被释放时的释放数；0 表示尚未确定。 */
    playback_service_half_kind_t Kind[PLAYBACK_SERVICE_PCM_HALF_COUNT];
} playback_service_output_t;

static playback_service_output_t s_out;
static bool s_pump_owner; /**< 本流已由 Pump 驱动；此后 Advance/ReadFrame 不可再用。 */

/** @brief PCM 半区：两个半区连续，16 位立体声交错，位于 AXI SRAM NOLOAD 段（HWD-6、HWD-2）。 */
static int16_t s_pcm[PLAYBACK_SERVICE_PCM_HALF_COUNT][PLAYBACK_SERVICE_PCM_HALF_SAMPLES]
    __attribute__((section(PLAYBACK_SERVICE_PCM_SECTION), aligned(PLATFORM_DCACHE_LINE_SIZE)));

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
 * @brief 重置输出管线到“没有输出”的状态；STOP_FAILED 锁存只由 Init 解除，这里保留。
 */
static void reset_output(void)
{
    const bool faulted = s_out.Faulted;

    memset(&s_out, 0, sizeof(s_out));
    s_out.Faulted = faulted;
    s_frame_offset = 0U;
    s_pump_owner = false;
}

/**
 * @brief 同步停止 Platform 输出；失败即锁存。
 * @return SERVICE_OK 已停止（含本来就未运行）；SERVICE_ERROR 表示 Stop 失败，Platform 已锁存 STOP_FAILED，
 *         此后模块不再写 PCM 半区。
 * @note  Platform_Audio_Stop() 在调用者上下文阻塞，最坏约为静音斜坡加两次 DMA Abort 超时（E4）。
 */
static Service_StatusTypeDef stop_output(void)
{
    if (s_out.Running)
    {
        s_out.Running = false;
        if (Platform_Audio_Stop() != PLATFORM_OK)
        {
            s_out.Faulted = true;
            return SERVICE_ERROR;
        }
    }
    return SERVICE_OK;
}

/**
 * @brief 初始化播放模块的静态状态。
 * @return SERVICE_OK。可重复调用；调用后当前流作废，需重新 BeginStream()。
 * @note   不分配内存，不访问硬件，不调用 Platform（输出若在运行，由调用者先 Stop）；任务上下文调用。
 *         清除 STOP_FAILED 锁存记忆，所以只应在启动时调用。PCM 半区内容在此写成 0，首次 Start 之前
 *         还会被预填完整覆盖（NOLOAD 段内容开机无效，HWD-2）。
 */
Service_StatusTypeDef Service_Playback_Init(void)
{
    memset(&s_stream, 0, sizeof(s_stream));
    s_stream_active = false;
    s_window_fill = 0U;
    s_frame_samples = 0U;
    s_frame_channels = 0U;
    s_frame_pending = false;
    memset(&s_out, 0, sizeof(s_out));
    s_frame_offset = 0U;
    s_pump_owner = false;
    memset(s_pcm, 0, sizeof(s_pcm));
    playback_service_decoder_reset();
    s_initialized = true;
    return SERVICE_OK;
}

/**
 * @brief 开始新流：重置解码器、线性输入窗口、已解码时间、同步预算与输出管线。
 * @param[in] audio_bytes 音频区间字节数（开启 OK 带回），仅用于流头缺失时估算总时长；0 表示未知。
 * @return SERVICE_OK；未 Init 或 STOP_FAILED 已锁存返回 SERVICE_NOT_READY；上一条流的输出仍在运行且
 *         Stop 失败返回 SERVICE_ERROR。
 * @note   不认识曲目或流序号；换曲、重选同一首都调用本函数。上一条流若仍在输出会先 Stop（等同
 *         Service_Playback_AbandonStream()）；调用者通常已先放弃或已收到流尾放完。
 */
Service_StatusTypeDef Service_Playback_BeginStream(uint32_t audio_bytes)
{
    if (!s_initialized || s_out.Faulted)
    {
        return SERVICE_NOT_READY;
    }
    if (stop_output() != SERVICE_OK)
    {
        return SERVICE_ERROR;
    }

    memset(&s_stream, 0, sizeof(s_stream));
    s_stream.AudioBytes = audio_bytes;
    s_window_fill = 0U;
    s_frame_pending = false;
    reset_output();
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
 * @brief 解码一步：最多对窗口头部做一次解码调用，并回报事实（Advance 与输出管线共用）。
 * @param[out] report 本次推进的结果与事实，总是被写。
 * @note   调用者保证有活动流。上一帧未被取走时将其丢弃。解码门槛、同步预算与首帧处理见
 *         Service_Playback_Advance() 的说明。
 */
static void decode_step(Service_Playback_AdvanceReportTypeDef *report)
{
    playback_service_decoder_info_t info;
    uint32_t samples;
    uint32_t consumed;

    memset(report, 0, sizeof(*report));
    report->StreamInfo = s_stream.Info;
    s_frame_pending = false;
    s_frame_offset = 0U;

    if (s_stream.SyncExhausted)
    {
        report->Result = SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED;
        report->DecodedMs = decoded_ms();
        return;
    }

    if ((s_window_fill < PLAYBACK_SERVICE_WINDOW_MIN_BYTES) && !s_stream.InputEnded)
    {
        report->Result = SERVICE_PLAYBACK_ADVANCE_NEED_INPUT;
        report->DecodedMs = decoded_ms();
        return;
    }
    if (s_window_fill == 0U)
    {
        report->Result = SERVICE_PLAYBACK_ADVANCE_END;
        report->DecodedMs = decoded_ms();
        return;
    }

    samples = playback_service_decoder_decode(s_window, s_window_fill, s_frame_pcm, &info);
    consumed = info.ConsumedBytes;

    if (samples == 0U)
    {
        window_consume(consumed);
        count_unsynced(consumed);
        report->Result = s_stream.SyncExhausted ? SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED : SERVICE_PLAYBACK_ADVANCE_SKIPPED;
        report->DecodedMs = decoded_ms();
        return;
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
            return;
        }
    }

    window_consume(consumed);

    if (s_stream.FirstFrameDecoded && (info.SampleRateHz != s_stream.Info.SampleRateHz))
    {
        count_unsynced(consumed);
        report->Result = s_stream.SyncExhausted ? SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED : SERVICE_PLAYBACK_ADVANCE_SKIPPED;
        report->DecodedMs = decoded_ms();
        return;
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
    s_frame_offset = 0U;

    report->Result = SERVICE_PLAYBACK_ADVANCE_FRAME;
    report->PcmFrames = samples;
    report->DecodedMs = decoded_ms();
}


/**
 * @brief 推进解码：最多对窗口头部做一次解码调用，并回报事实。
 * @param[out] report 本次推进的结果与事实；返回非 SERVICE_OK 时不写。
 * @return SERVICE_OK；参数为空返回 SERVICE_INVALID_PARAM；无活动流返回 SERVICE_NOT_READY；本流已由
 *         Service_Playback_Pump() 驱动返回 SERVICE_BUSY（两条路径不可在同一条流上混用）。
 * @note   解码门槛（D3）：窗口字节数低于 W_min 且输入未结束时不调用解码，回报 NEED_INPUT。
 *         同步预算（D4）：自音频区间开头或上一个好帧起连续 R 字节解不出帧回报 SYNC_EXHAUSTED；
 *         与首帧采样率不同的帧丢弃（不出 PCM、不计入已解码时间）并计入 R。
 *         首帧若是 Xing/Info 或 VBRI 元数据帧则跳过它，首帧信息在其后的第一个音频帧上回报。
 *         上一帧未被 ReadFrame() 取走时，本次推进将其丢弃。
 *         阻塞性：纯计算，约一帧解码耗时（板上耗时待实测）；栈占用见解码实现。
 */
Service_StatusTypeDef Service_Playback_Advance(Service_Playback_AdvanceReportTypeDef *report)
{
    if (report == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    if (!s_stream_active)
    {
        return SERVICE_NOT_READY;
    }

    if (s_pump_owner)
    {
        return SERVICE_BUSY;
    }

    decode_step(report);
    return SERVICE_OK;
}

/**
 * @brief 把帧输出缓冲里从 start 起的 count 个采样拷成立体声交错；单声道复制到左右声道。
 * @param[out] stereo_pcm 目标，容纳 count 个立体声帧。
 * @param[in]  start      帧输出缓冲内的起始采样序号。
 * @param[in]  count      要拷的每声道采样数，start + count 不超过当前帧的采样数。
 */
static void frame_copy_stereo(int16_t *stereo_pcm, uint32_t start, uint32_t count)
{
    uint32_t i;

    if (s_frame_channels == 1U)
    {
        for (i = 0U; i < count; i++)
        {
            stereo_pcm[2U * i] = s_frame_pcm[start + i];
            stereo_pcm[(2U * i) + 1U] = s_frame_pcm[start + i];
        }
    }
    else
    {
        memcpy(stereo_pcm, &s_frame_pcm[2U * start], (size_t)count * 2U * sizeof(int16_t));
    }
}

/**
 * @brief 把帧输出缓冲里的一帧 PCM 拷成立体声交错；单声道在此复制到左右声道。
 * @param[out] stereo_pcm      目标缓冲，容纳 capacity_frames 个立体声帧（每帧 2 个 int16_t）。
 * @param[in]  capacity_frames 目标缓冲的立体声帧容量。
 * @param[out] frames          实际写入的立体声帧数。
 * @return SERVICE_OK；参数为空返回 SERVICE_INVALID_PARAM；没有待取的帧返回 SERVICE_NOT_READY；
 *         容量不足返回 SERVICE_NO_SPACE（帧保留，可换更大的缓冲重取）；本流已由 Service_Playback_Pump()
 *         驱动返回 SERVICE_BUSY。
 * @note   一帧只能取走一次；下一次 Advance() 会丢弃未取走的帧。
 */
Service_StatusTypeDef Service_Playback_ReadFrame(int16_t *stereo_pcm, uint32_t capacity_frames, uint32_t *frames)
{
    if ((stereo_pcm == NULL) || (frames == NULL))
    {
        return SERVICE_INVALID_PARAM;
    }
    *frames = 0U;

    if (s_pump_owner)
    {
        return SERVICE_BUSY;
    }
    if (!s_frame_pending)
    {
        return SERVICE_NOT_READY;
    }
    if (capacity_frames < s_frame_samples)
    {
        return SERVICE_NO_SPACE;
    }

    frame_copy_stereo(stereo_pcm, 0U, s_frame_samples);
    *frames = s_frame_samples;
    s_frame_pending = false;
    return SERVICE_OK;
}

/** @brief 一次补半区的结局。 */
typedef enum
{
    PLAYBACK_SERVICE_FILL_DONE = 0U, /**< 半区已补成（满、补 0 收尾或整个静音）。 */
    PLAYBACK_SERVICE_FILL_STALLED,   /**< 输入不够，半区只补了一部分，进度保留。 */
    PLAYBACK_SERVICE_FILL_BAD,       /**< 本流不可播（同步预算耗尽，或一帧都没解出）。 */
    PLAYBACK_SERVICE_FILL_NO_AUDIO   /**< 本轮没有任何可放的音频：真实音频已在被丢弃的半区里放完。 */
} playback_service_fill_result_t;

/**
 * @brief 补满下一个要补的半区（Fills % 2）；半区编号与种类见 playback_service_output_t。
 * @param[in,out] report 累计本次推进的事实（首帧、需要输入、同步预算耗尽）。
 * @return 补半区的结局。
 * @note  解码读尽后：已写入的部分补 0 并标为最后的数据半区；一帧也没有则整个半区是静音，且若此前
 *        尚未确定最后的数据半区，则上一个半区就是最后的数据半区（流恰好在整半区处结束）。
 *        一帧跨半区时剩余部分留在帧输出缓冲，下一个半区先取它。
 */
static playback_service_fill_result_t fill_half(Service_Playback_PumpReportTypeDef *report)
{
    const uint32_t half = s_out.Fills & 1U;
    int16_t *dst = s_pcm[half];

    while (s_out.FillFrames < PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF)
    {
        Service_Playback_AdvanceReportTypeDef step;

        if (s_frame_pending)
        {
            const uint32_t room = PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF - s_out.FillFrames;
            const uint32_t left = s_frame_samples - s_frame_offset;
            const uint32_t count = (left < room) ? left : room;

            frame_copy_stereo(dst + (2U * s_out.FillFrames), s_frame_offset, count);
            s_out.FillFrames += count;
            s_frame_offset += count;
            if (s_frame_offset >= s_frame_samples)
            {
                s_frame_pending = false;
                s_frame_offset = 0U;
            }
            continue;
        }
        if (s_out.DecodeEnded)
        {
            break;
        }

        decode_step(&step);
        if (step.FirstFrame)
        {
            report->FirstFrame = true;
        }
        if (step.Result == SERVICE_PLAYBACK_ADVANCE_NEED_INPUT)
        {
            report->NeedInput = true;
            return PLAYBACK_SERVICE_FILL_STALLED;
        }
        if (step.Result == SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED)
        {
            report->SyncExhausted = true;
            return PLAYBACK_SERVICE_FILL_BAD;
        }
        if (step.Result == SERVICE_PLAYBACK_ADVANCE_END)
        {
            if (!s_stream.FirstFrameDecoded)
            {
                report->SyncExhausted = true;
                return PLAYBACK_SERVICE_FILL_BAD;
            }
            s_out.DecodeEnded = true;
        }
    }

    if (s_out.FillFrames == 0U)
    {
        if (s_out.Fills == 0U)
        {
            return PLAYBACK_SERVICE_FILL_NO_AUDIO;
        }
        memset(dst, 0, sizeof(s_pcm[0]));
        s_out.Kind[half] = PLAYBACK_SERVICE_HALF_SILENCE;
        if (s_out.LastRelease == 0U)
        {
            s_out.Kind[half ^ 1U] = PLAYBACK_SERVICE_HALF_LAST;
            s_out.LastRelease = s_out.Fills;
        }
    }
    else if (s_out.FillFrames < PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF)
    {
        memset(dst + (2U * s_out.FillFrames), 0, (size_t)(PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF - s_out.FillFrames) * 2U * sizeof(int16_t));
        s_out.Kind[half] = PLAYBACK_SERVICE_HALF_LAST;
        s_out.LastRelease = s_out.Fills + 1U;
    }
    else
    {
        s_out.Kind[half] = PLAYBACK_SERVICE_HALF_DATA;
    }

    s_out.FillFrames = 0U;
    s_out.Fills++;
    return PLAYBACK_SERVICE_FILL_DONE;
}

/**
 * @brief 补满当前需要补的半区，直到凑齐 Released + 2 次补满；运行中每补成一个就提交。
 * @param[in,out] report 累计本次推进的事实。
 * @return SERVICE_OK；SubmitHalf 失败返回 SERVICE_ERROR。
 * @note  不运行（预填、暂停）时只写半区不提交。输入不够、不可播或没有可放的音频时提前返回，状态记在
 *        report 与 s_out 里。最多补 2 个半区（B1 的结构上界）。
 */
static Service_StatusTypeDef fill_pending(Service_Playback_PumpReportTypeDef *report)
{
    while ((s_out.Fills < (s_out.Released + 2U)) && !s_out.EndedPending)
    {
        const uint32_t half = s_out.Fills & 1U;
        const playback_service_fill_result_t result = fill_half(report);

        if (result == PLAYBACK_SERVICE_FILL_NO_AUDIO)
        {
            s_out.EndedPending = true;
            break;
        }
        if (result != PLAYBACK_SERVICE_FILL_DONE)
        {
            break;
        }
        if (s_out.Running && (Platform_Audio_SubmitHalf(half) != PLATFORM_OK))
        {
            return SERVICE_ERROR;
        }
    }
    return SERVICE_OK;
}

/**
 * @brief 把两个半区的内容与种类交换，使“暂停时 DMA 正在读的半区”回到半区 0（D9）。
 */
static void swap_halves(void)
{
    uint32_t i;
    playback_service_half_kind_t kind;

    for (i = 0U; i < PLAYBACK_SERVICE_PCM_HALF_SAMPLES; i++)
    {
        const int16_t tmp = s_pcm[0][i];

        s_pcm[0][i] = s_pcm[1][i];
        s_pcm[1][i] = tmp;
    }
    kind = s_out.Kind[0];
    s_out.Kind[0] = s_out.Kind[1];
    s_out.Kind[1] = kind;
}

/**
 * @brief 两个半区都已补好、输出未运行时 Start；同曲恢复时先整理半区顺序。
 * @param[in,out] report 累计事实；可为 NULL（恢复路径不回报，事实留给下一次推进）。
 * @return SERVICE_OK；Start 被拒（参数或锁存）返回 SERVICE_ERROR，状态记在 s_out，report 非空时也回报。
 * @note  同曲恢复（Released 大于 0）：Start 总从半区 0 开始，而暂停时 DMA 正在读的是半区 Released % 2，
 *        必要时交换两个半区，使该半区回到半区 0 从头重放，另一半区接在后面。Start 之后释放计数从 0 起。
 */
static Service_StatusTypeDef start_output(Service_Playback_PumpReportTypeDef *report)
{
    Platform_Audio_StatusTypeDef status;

    if ((s_out.Released & 1U) != 0U)
    {
        swap_halves();
    }
    s_out.Fills = PLAYBACK_SERVICE_PCM_HALF_COUNT;
    s_out.Released = 0U;
    s_out.LastRelease = (s_out.Kind[0] == PLAYBACK_SERVICE_HALF_LAST) ? 1U : ((s_out.Kind[1] == PLAYBACK_SERVICE_HALF_LAST) ? 2U : 0U);

    if (Platform_Audio_Start(s_stream.Info.SampleRateHz, &s_pcm[0][0], PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF) == PLATFORM_OK)
    {
        s_out.Running = true;
        s_out.SeenUnderruns = 0U;
        if (report != NULL)
        {
            report->OutputStarted = true;
        }
        return SERVICE_OK;
    }

    if ((Platform_Audio_GetStatus(&status) == PLATFORM_OK) && (status.Fault == PLATFORM_AUDIO_FAULT_STOP_FAILED))
    {
        s_out.Faulted = true;
        if (report != NULL)
        {
            report->StopFailed = true;
        }
    }
    else
    {
        s_out.Rejected = true;
        if (report != NULL)
        {
            report->StartRejected = true;
        }
    }
    return SERVICE_ERROR;
}

/**
 * @brief 输出未运行、半区已补好且没有暂停时 Start。
 * @param[in,out] report 累计事实。
 * @return SERVICE_OK 或 SERVICE_ERROR（见 start_output）。
 */
static Service_StatusTypeDef start_if_ready(Service_Playback_PumpReportTypeDef *report)
{
    if (!s_out.Running && !s_out.Paused && !s_out.EndedPending && !s_out.Faulted && !s_out.Rejected &&
        (s_out.Fills == (s_out.Released + 2U)) && (s_stream.Info.SampleRateHz != 0U))
    {
        return start_output(report);
    }
    return SERVICE_OK;
}

/**
 * @brief 欠载恢复：Stop，丢弃全部半区（欠载时两个半区都已陈旧），重新补满，随后由调用者 Start（D8）。
 * @param[in,out] report 累计事实。
 * @return SERVICE_OK；Stop 失败返回 SERVICE_ERROR，已锁存。
 * @note  解码进度不回退：陈旧半区里没放出去的音频至多两个半区，直接丢弃；已解码时间保持。
 */
static Service_StatusTypeDef recover_from_underrun(Service_Playback_PumpReportTypeDef *report)
{
    if (stop_output() != SERVICE_OK)
    {
        report->StopFailed = true;
        return SERVICE_ERROR;
    }
    s_out.Fills = 0U;
    s_out.FillFrames = 0U;
    s_out.Released = 0U;
    s_out.LastRelease = 0U;
    return SERVICE_OK;
}

/**
 * @brief 推进输出管线：读 Platform 事实，处理流尾与欠载，补满空出的半区并提交，必要时 Start，返回事实。
 * @param[out] report 本次推进的事实；返回非 SERVICE_OK 时内容无意义。
 * @return SERVICE_OK；参数为空返回 SERVICE_INVALID_PARAM；无活动流返回 SERVICE_NOT_READY；
 *         读状态或提交半区失败返回 SERVICE_ERROR（调用者按音频错误处理）。
 * @note   顺序（B1）：Platform 状态与故障 → 流尾放完 → 欠载 → 补半区并提交 → 预填完成则 Start。
 *         每次推进最多补 2 个半区、解码 2 个半区的帧；输入不够就回报 NeedInput 并保留进度。
 *         不阻塞等待输入；Start/Stop 的阻塞见 Platform_Audio_Start()/Platform_Audio_Stop()。
 *         STOP_FAILED 之后只回报事实，不再写 PCM 半区。任务上下文调用。
 */
Service_StatusTypeDef Service_Playback_Pump(Service_Playback_PumpReportTypeDef *report)
{
    Platform_Audio_StatusTypeDef status;
    Service_StatusTypeDef result = SERVICE_OK;

    if (report == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }
    if (!s_stream_active)
    {
        return SERVICE_NOT_READY;
    }

    memset(report, 0, sizeof(*report));
    s_pump_owner = true;

    if (s_out.Faulted)
    {
        report->StopFailed = true;
    }
    else if (s_out.Rejected)
    {
        report->StartRejected = true;
    }
    else if (s_out.EndedPending)
    {
        report->StreamEnded = true;
    }
    else
    {
        bool stop_here = false;

        if (s_out.Running)
        {
            if (Platform_Audio_GetStatus(&status) != PLATFORM_OK)
            {
                return SERVICE_ERROR;
            }
            if (status.Fault == PLATFORM_AUDIO_FAULT_STOP_FAILED)
            {
                s_out.Faulted = true;
                report->StopFailed = true;
                stop_here = true;
            }
            else if (status.Fault == PLATFORM_AUDIO_FAULT_TRANSFER_ERROR)
            {
                report->TransferError = true;
                stop_here = true;
            }
            else
            {
                if (status.ReleasedHalves != s_out.Released)
                {
                    report->OutputProgress = true;
                }
                s_out.Released = status.ReleasedHalves;

                if ((s_out.LastRelease != 0U) && (s_out.Released >= s_out.LastRelease))
                {
                    stop_here = true;
                    if (stop_output() == SERVICE_OK)
                    {
                        s_out.EndedPending = true;
                        report->StreamEnded = true;
                    }
                    else
                    {
                        report->StopFailed = true;
                    }
                }
                else if (status.Underruns != s_out.SeenUnderruns)
                {
                    report->Underruns = status.Underruns - s_out.SeenUnderruns;
                    if (recover_from_underrun(report) != SERVICE_OK)
                    {
                        stop_here = true;
                    }
                }
            }
        }

        if (!stop_here)
        {
            result = fill_pending(report);
            if ((result == SERVICE_OK) && (start_if_ready(report) != SERVICE_OK))
            {
                result = SERVICE_OK; /* 拒绝已作为事实回报，不是调用错误。 */
            }
            if (s_out.EndedPending)
            {
                report->StreamEnded = true;
            }
        }
    }

    report->StreamInfo = s_stream.Info;
    report->DecodedMs = decoded_ms();
    return result;
}

/**
 * @brief 暂停输出：Stop（含软静音），流保持；暂停态下继续把空出的半区补满但不 Start（B12）。
 * @return SERVICE_OK；无活动流返回 SERVICE_NOT_READY；Stop 失败返回 SERVICE_ERROR（已锁存 STOP_FAILED，
 *         此后模块不再写 PCM 半区）；STOP_FAILED 早已锁存也返回 SERVICE_ERROR。
 * @note   阻塞：Platform_Audio_Stop() 同步执行，最坏约为静音斜坡加两次 DMA Abort 超时。
 *         Stop 返回 OK 后 Platform 计数冻结，记下释放数以便恢复时重放 DMA 正在读的半区（D9）。
 *         若暂停时已读完的半区多于两个（连续欠载），补满会覆盖尚未重放的半区，丢弃半区，恢复时按解码进度重新补满。
 *         重复暂停无副作用。任务上下文调用。
 */
Service_StatusTypeDef Service_Playback_PauseOutput(void)
{
    Platform_Audio_StatusTypeDef status;

    if (!s_stream_active)
    {
        return SERVICE_NOT_READY;
    }
    if (s_out.Faulted)
    {
        return SERVICE_ERROR;
    }
    if (s_out.Paused)
    {
        return SERVICE_OK;
    }

    s_out.Paused = true;
    if (s_out.Running)
    {
        if (stop_output() != SERVICE_OK)
        {
            return SERVICE_ERROR;
        }
        if (Platform_Audio_GetStatus(&status) != PLATFORM_OK)
        {
            return SERVICE_ERROR;
        }
        s_out.Released = status.ReleasedHalves;
        if ((s_out.LastRelease != 0U) && (s_out.Released >= s_out.LastRelease))
        {
            s_out.EndedPending = true;
        }
        else if (s_out.Fills < s_out.Released)
        {
            /* 释放数超出补满数：按序补满会覆盖尚未重放的半区，直接重来。 */
            s_out.Fills = 0U;
            s_out.FillFrames = 0U;
            s_out.Released = 0U;
            s_out.LastRelease = 0U;
        }
    }
    return SERVICE_OK;
}

/**
 * @brief 恢复输出：补好暂停期间空出的半区后 Start；同曲恢复从暂停时 DMA 正在读的半区开头重放（D9）。
 * @return SERVICE_OK（含输入不够、等下一次 Pump 继续补满后再 Start）；无活动流返回 SERVICE_NOT_READY；
 *         Start 被拒或 STOP_FAILED 已锁存返回 SERVICE_ERROR，事实在下一次 Pump 回报。
 * @note   阻塞：Platform_Audio_Start()；最多补 2 个半区。重复恢复无副作用。任务上下文调用。
 */
Service_StatusTypeDef Service_Playback_ResumeOutput(void)
{
    Service_Playback_PumpReportTypeDef report;

    if (!s_stream_active)
    {
        return SERVICE_NOT_READY;
    }
    if (s_out.Faulted)
    {
        return SERVICE_ERROR;
    }
    if (!s_out.Paused)
    {
        return SERVICE_OK;
    }

    s_out.Paused = false;
    s_pump_owner = true;
    memset(&report, 0, sizeof(report));
    if (fill_pending(&report) != SERVICE_OK)
    {
        return SERVICE_ERROR;
    }
    return start_if_ready(&report);
}

/**
 * @brief 放弃当前流：Stop 输出，丢弃半区内容，使流失效；换曲与停止都先调用它（B11）。
 * @return SERVICE_OK；Stop 失败返回 SERVICE_ERROR（已锁存 STOP_FAILED）；模块未 Init 返回 SERVICE_NOT_READY。
 * @note   阻塞：Platform_Audio_Stop()。解码器与输入窗口在下一次 BeginStream() 重置。重复调用无副作用。
 *         任务上下文调用。
 */
Service_StatusTypeDef Service_Playback_AbandonStream(void)
{
    Service_StatusTypeDef result;

    if (!s_initialized)
    {
        return SERVICE_NOT_READY;
    }

    result = stop_output();
    reset_output();
    s_stream_active = false;
    s_window_fill = 0U;
    s_frame_pending = false;
    return result;
}
