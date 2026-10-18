/**
 * @file fake_platform_audio.c
 * @brief 缝 2 的假 Platform 音频实现，见 fake_platform_audio.h。
 * @note  DMA 读到的是 Start/SubmitHalf 时“Clean”出去的副本，而不是调用者缓冲的实时内容：
 *        写了半区却忘记 SubmitHalf 的缺陷会让播放日志里出现旧数据，而不是被实时内存掩盖。
 */

#include "Tests/playback_service/fake_platform_audio.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_LINE_BYTES (32U)
#define FAKE_HALF_COUNT (2U)
#define FAKE_MAX_HALF_FRAMES (4096U)

fake_audio_state_t g_fake_audio;

static Platform_Audio_EventCallback_t s_callback;
static void *s_callback_context;
static uint32_t s_callback_count;

static uint32_t s_released;
static uint32_t s_underruns;
static Platform_Audio_FaultTypeDef s_fault;
static int s_submitted[FAKE_HALF_COUNT];
static int16_t *s_ring;
static uint32_t s_frames_per_half;
static int16_t s_clean[FAKE_HALF_COUNT][FAKE_MAX_HALF_FRAMES * 2U];

static int s_fail_next_stop;
static int s_reject_next_start;
static int s_fail_next_get_status;
static uint32_t s_extra_rate;

static int16_t *s_played;
static size_t s_played_samples;
static size_t s_played_capacity;

/**
 * @brief 记录一次调用。
 * @param[in] kind 调用种类。
 * @param[in] arg  调用参数。
 */
static void record_call(fake_audio_call_kind_t kind, uint32_t arg)
{
    assert(g_fake_audio.CallCount < FAKE_AUDIO_MAX_CALLS);
    g_fake_audio.Calls[g_fake_audio.CallCount].Kind = kind;
    g_fake_audio.Calls[g_fake_audio.CallCount].Arg = arg;
    g_fake_audio.CallCount++;
}

/**
 * @brief 把调用者缓冲里的一个半区 Clean 到 DMA 可见的副本。
 * @param[in] half 半区号。
 */
static void clean_half(uint32_t half)
{
    memcpy(s_clean[half], s_ring + ((size_t)half * s_frames_per_half * 2U), (size_t)s_frames_per_half * 2U * sizeof(int16_t));
}

/**
 * @brief 采样率是否被接受。
 * @param[in] rate 采样率。
 * @return 非 0 表示接受。
 */
static int rate_supported(uint32_t rate)
{
    return (rate == 32000U) || (rate == 44100U) || (rate == 48000U) || ((s_extra_rate != 0U) && (rate == s_extra_rate));
}

/**
 * @brief 复位假对象。
 */
void fake_audio_reset(void)
{
    free(g_fake_audio.StartSnapshot);
    free(s_played);
    memset(&g_fake_audio, 0, sizeof(g_fake_audio));
    s_callback = NULL;
    s_callback_context = NULL;
    s_callback_count = 0U;
    s_released = 0U;
    s_underruns = 0U;
    s_fault = PLATFORM_AUDIO_FAULT_NONE;
    s_submitted[0] = 0;
    s_submitted[1] = 0;
    s_ring = NULL;
    s_frames_per_half = 0U;
    s_fail_next_stop = 0;
    s_reject_next_start = 0;
    s_fail_next_get_status = 0;
    s_extra_rate = 0U;
    s_played = NULL;
    s_played_samples = 0U;
    s_played_capacity = 0U;
}

/**
 * @brief 取回已播放日志。
 * @param[out] samples 日志的 int16 样本数。
 * @return 日志起点；无记录时可为 NULL。
 */
const int16_t *fake_audio_played(size_t *samples)
{
    *samples = s_played_samples;
    return s_played;
}

/**
 * @brief 把一个半区的副本追加到播放日志。
 * @param[in] half 半区号。
 */
static void append_played(uint32_t half)
{
    const size_t count = (size_t)s_frames_per_half * 2U;

    if ((s_played_samples + count) > s_played_capacity)
    {
        s_played_capacity = (s_played_capacity == 0U) ? (count * 64U) : (s_played_capacity * 2U);
        s_played = (int16_t *)realloc(s_played, s_played_capacity * sizeof(int16_t));
        assert(s_played != NULL);
    }
    memcpy(s_played + s_played_samples, s_clean[half], count * sizeof(int16_t));
    s_played_samples += count;
}

/**
 * @brief 调用已注册的事件回调（模拟 ISR 唤醒）。
 */
static void fire_callback(void)
{
    s_callback_count++;
    if (s_callback != NULL)
    {
        s_callback(s_callback_context);
    }
}

/**
 * @brief 模拟 DMA 读完一个半区：计入已读完的半区日志与释放数，另一半区未提交则欠载加一，随后调用事件回调。
 * @return 1 表示发生了释放；输出未运行（含 Stop 之后）时不释放，返回 0。
 */
int fake_audio_release_half(void)
{
    uint32_t half;
    uint32_t other;

    if (!g_fake_audio.Running)
    {
        return 0;
    }

    half = s_released & 1U;
    other = half ^ 1U;
    if (s_submitted[half] != 0)
    {
        append_played(half);
    }
    s_released++;
    if (s_submitted[other] == 0)
    {
        s_underruns++;
    }
    s_submitted[half] = 0;
    fire_callback();
    return 1;
}

/**
 * @brief 注入 DMA 传输错误：记 TRANSFER_ERROR 并调用回调；输出仍视为运行，等待调用者 Stop。
 * @note  输出必须正在运行；STOP_FAILED 已锁存时保持锁存，不被覆盖。
 */
void fake_audio_inject_transfer_error(void)
{
    assert(g_fake_audio.Running);
    if (s_fault != PLATFORM_AUDIO_FAULT_STOP_FAILED)
    {
        s_fault = PLATFORM_AUDIO_FAULT_TRANSFER_ERROR;
    }
    fire_callback();
}

/**
 * @brief 让下一次真正停止输出的 Stop 失败：锁存 STOP_FAILED 并返回错误（缓冲留在 Platform）。
 * @note  只对输出正在运行时的 Stop 生效一次。
 */
void fake_audio_fail_next_stop(void)
{
    s_fail_next_stop = 1;
}

/**
 * @brief 直接锁存 STOP_FAILED（模拟 Stop 之外的时机已锁存），之后 Start/Stop 一律拒绝。
 */
void fake_audio_latch_stop_failed(void)
{
    s_fault = PLATFORM_AUDIO_FAULT_STOP_FAILED;
}

/**
 * @brief 让下一次 Platform_Audio_GetStatus 返回错误（仅一次）。
 * @note  只影响状态读取；Start、Stop 与 SubmitHalf 不受影响，也不改变计数与故障。
 */
void fake_audio_fail_next_get_status(void)
{
    s_fail_next_get_status = 1;
}

/**
 * @brief 让下一次 Start 因参数被拒（仅一次，不锁存，Fault 保持不变）。
 */
void fake_audio_reject_next_start(void)
{
    s_reject_next_start = 1;
}

/**
 * @brief 额外接受一个采样率（默认只收 32000、44100、48000）。
 * @param[in] rate 采样率。
 * @note  仅用于用 MPEG-2 夹具构造“非整半区”的流尾；真实 Platform 仍拒绝它，生产合同不变。
 */
void fake_audio_accept_rate_for_test(uint32_t rate)
{
    s_extra_rate = rate;
}

/**
 * @brief 取回某半区最近一次 Clean 出去（Start 或 SubmitHalf）的内容，即 DMA 将读到的数据。
 * @param[in] half 半区号，必须小于 2。
 * @return 该半区副本的首地址（立体声交错 int16）。
 */
const int16_t *fake_audio_clean_half(uint32_t half)
{
    assert(half < FAKE_HALF_COUNT);
    return s_clean[half];
}

/**
 * @brief 事件回调被调用的次数（ISR 唤醒的模拟）。
 * @return 自上次 fake_audio_reset() 起的调用次数，未注册回调时也计数。
 */
uint32_t fake_audio_callback_count(void)
{
    return s_callback_count;
}

/**
 * @brief 假 Platform_Audio_Init：无硬件可初始化。
 * @return PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Audio_Init(void)
{
    return PLATFORM_OK;
}

/**
 * @brief 假 Platform_Audio_SetEventCallback：登记半区释放与故障的事件回调。
 * @param[in] callback 事件回调，不得为 NULL。
 * @param[in] context  回调的用户上下文，原样回传。
 * @return PLATFORM_OK；callback 为 NULL 返回 PLATFORM_AUDIO_ERROR。
 */
Platform_StatusTypeDef Platform_Audio_SetEventCallback(Platform_Audio_EventCallback_t callback, void *context)
{
    if (callback == NULL)
    {
        return PLATFORM_AUDIO_ERROR;
    }
    s_callback = callback;
    s_callback_context = context;
    return PLATFORM_OK;
}

/**
 * @brief 假 Platform_Audio_ClearEventCallback：清除已登记的事件回调。
 * @return PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Audio_ClearEventCallback(void)
{
    s_callback = NULL;
    s_callback_context = NULL;
    return PLATFORM_OK;
}

/**
 * @brief 假 Platform_Audio_Start：按合同校验参数后清零计数并开始输出，两个半区视为已提交。
 * @param[in] sample_rate     采样率，只收 32/44.1/48 kHz（另可由 fake_audio_accept_rate_for_test() 追加）。
 * @param[in] ring            双半区缓冲首地址，须 32 B 对齐，不得为 NULL。
 * @param[in] frames_per_half 每半区立体声帧数，非 0，半区字节数须为 32 B 的整数倍。
 * @return PLATFORM_OK；STOP_FAILED 已锁存、运行中再 Start（记合同违反）、被 fake_audio_reject_next_start() 拒绝
 *         或参数不被接受返回 PLATFORM_AUDIO_ERROR，且不改变现有输出。
 */
Platform_StatusTypeDef Platform_Audio_Start(uint32_t sample_rate, int16_t *ring, uint32_t frames_per_half)
{
    const uint32_t half_bytes = frames_per_half * 2U * (uint32_t)sizeof(int16_t);

    if (s_fault == PLATFORM_AUDIO_FAULT_STOP_FAILED)
    {
        return PLATFORM_AUDIO_ERROR;
    }
    if (g_fake_audio.Running)
    {
        g_fake_audio.ContractViolations++;
        return PLATFORM_AUDIO_ERROR;
    }
    if (s_reject_next_start != 0)
    {
        s_reject_next_start = 0;
        return PLATFORM_AUDIO_ERROR;
    }
    if (!rate_supported(sample_rate) || (ring == NULL) || (frames_per_half == 0U) || (frames_per_half > FAKE_MAX_HALF_FRAMES) ||
        ((((uintptr_t)ring) % FAKE_LINE_BYTES) != 0U) || ((half_bytes % FAKE_LINE_BYTES) != 0U))
    {
        return PLATFORM_AUDIO_ERROR;
    }

    s_ring = ring;
    s_frames_per_half = frames_per_half;
    clean_half(0U);
    clean_half(1U);
    s_released = 0U;
    s_underruns = 0U;
    s_fault = PLATFORM_AUDIO_FAULT_NONE;
    s_submitted[0] = 1;
    s_submitted[1] = 1;
    g_fake_audio.Running = 1;

    free(g_fake_audio.StartSnapshot);
    g_fake_audio.StartSnapshot = (int16_t *)malloc((size_t)frames_per_half * 2U * FAKE_HALF_COUNT * sizeof(int16_t));
    assert(g_fake_audio.StartSnapshot != NULL);
    memcpy(g_fake_audio.StartSnapshot, ring, (size_t)frames_per_half * 2U * FAKE_HALF_COUNT * sizeof(int16_t));
    g_fake_audio.StartCount++;
    g_fake_audio.StartRate = sample_rate;
    g_fake_audio.StartFrames = frames_per_half;
    g_fake_audio.StartRing = ring;
    record_call(FAKE_AUDIO_CALL_START, sample_rate);
    return PLATFORM_OK;
}

/**
 * @brief 假 Platform_Audio_SubmitHalf：把调用者缓冲里的半区 Clean 成 DMA 可见的副本并标记已提交。
 * @param[in] half 半区号，0 或 1。
 * @return PLATFORM_OK；半区号越界或输出未运行（记合同违反）返回 PLATFORM_AUDIO_ERROR。
 * @note  提交仍处于已提交态的半区记一次所有权违反（OwnershipViolations）。
 */
Platform_StatusTypeDef Platform_Audio_SubmitHalf(uint32_t half)
{
    if ((half >= FAKE_HALF_COUNT) || !g_fake_audio.Running)
    {
        g_fake_audio.ContractViolations++;
        return PLATFORM_AUDIO_ERROR;
    }
    if (s_submitted[half] != 0)
    {
        g_fake_audio.OwnershipViolations++;
    }
    clean_half(half);
    s_submitted[half] = 1;
    g_fake_audio.SubmitCount++;
    record_call(FAKE_AUDIO_CALL_SUBMIT, half);
    return PLATFORM_OK;
}

/**
 * @brief 假 Platform_Audio_GetStatus：读取释放数、欠载数与故障。
 * @param[out] status 状态快照。
 * @return PLATFORM_OK；status 为 NULL 或被 fake_audio_fail_next_get_status() 指定失败时返回 PLATFORM_AUDIO_ERROR。
 */
Platform_StatusTypeDef Platform_Audio_GetStatus(Platform_Audio_StatusTypeDef *status)
{
    if (status == NULL)
    {
        return PLATFORM_AUDIO_ERROR;
    }
    if (s_fail_next_get_status != 0)
    {
        s_fail_next_get_status = 0;
        return PLATFORM_AUDIO_ERROR;
    }
    status->ReleasedHalves = s_released;
    status->Underruns = s_underruns;
    status->Fault = s_fault;
    return PLATFORM_OK;
}

/**
 * @brief 假 Platform_Audio_Stop：停止输出，成功后计数冻结。
 * @return PLATFORM_OK（含本来就未运行）；STOP_FAILED 已锁存或被 fake_audio_fail_next_stop() 指定失败时返回
 *         PLATFORM_AUDIO_ERROR（后者锁存 STOP_FAILED）。
 * @note  每次调用都计入 StopCount 与调用记录。
 */
Platform_StatusTypeDef Platform_Audio_Stop(void)
{
    if (s_fault == PLATFORM_AUDIO_FAULT_STOP_FAILED)
    {
        g_fake_audio.StopCount++;
        record_call(FAKE_AUDIO_CALL_STOP, 0U);
        return PLATFORM_AUDIO_ERROR;
    }
    g_fake_audio.StopCount++;
    record_call(FAKE_AUDIO_CALL_STOP, 0U);
    if (!g_fake_audio.Running)
    {
        return PLATFORM_OK;
    }

    g_fake_audio.Running = 0;
    if (s_fail_next_stop != 0)
    {
        s_fail_next_stop = 0;
        s_fault = PLATFORM_AUDIO_FAULT_STOP_FAILED;
        return PLATFORM_AUDIO_ERROR;
    }
    return PLATFORM_OK;
}
