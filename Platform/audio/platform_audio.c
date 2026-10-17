/**
  ******************************************************************************
  * @file    platform_audio.c
  * @brief   本板 Audio Device、I2S2 循环 DMA 和 PCM5102A 静音编排的装配实现。
  *
  * @details
  *          Platform 持有输出事实计数、半区提交标志、故障锁存和 XSMT 时序；
  *          HAL I2S DMA 差异在 Adapters/stm32_hal/audio_i2s 内。ISR 事件只累加
  *          计数并调用单槽回调，不做 Cache、日志或阻塞操作。
  ******************************************************************************
  */

#include "Platform/audio/platform_audio.h"

#include <stdbool.h>
#include <stddef.h>

#include "Adapters/cortex/cache/cortex_m7_dcache_adapter.h"
#include "Adapters/stm32_hal/audio_i2s/audio_i2s_stm32_hal_adapter.h"
#include "Components/audio/audio.h"
#include "Components/log/log.h"
#include "i2s.h"
#include "main.h"

/** @brief PCM5102A 软静音斜坡等待时间（ms），覆盖 32 kHz 下约 104 个 LRCK 周期并留余量。hw:pending 待实测。 */
#define PLATFORM_AUDIO_MUTE_RAMP_MS        4U
/** @brief DMA 启动后到 DAC 就绪、可以解除静音的等待时间（ms）。hw:pending 待实测。 */
#define PLATFORM_AUDIO_DAC_READY_MS        3U
/** @brief 每个声道的采样为 16 位，立体声每帧 2 个数据单元。 */
#define PLATFORM_AUDIO_ITEMS_PER_FRAME     2U
/** @brief 循环缓冲区的半区数量。 */
#define PLATFORM_AUDIO_HALF_COUNT          2U
/** @brief HAL I2S DMA 单次传输的数据单元上限。 */
#define PLATFORM_AUDIO_MAX_ITEMS           65535U

/**
  * @brief 本板音频输出的私有状态。
  * @note  Released、Underruns、Fault、Submitted、Running、Callback 由 ISR 与任务
  *        共享，任务侧只在关中断的短临界区内读写它们。
  */
typedef struct
{
    Audio_HandleTypeDef Device;           /**< 本板唯一 Audio Device 实例。 */
    volatile uint32_t Released;           /**< 本次 Start 以来释放的半区数。 */
    volatile uint32_t Underruns;          /**< 本次 Start 以来的欠载数。 */
    volatile Platform_Audio_FaultTypeDef Fault; /**< 当前故障。 */
    volatile bool Submitted[PLATFORM_AUDIO_HALF_COUNT]; /**< 各半区是否已提交。 */
    volatile bool Running;                /**< 输出已启动且尚未成功 Stop。 */
    int16_t *Ring;                        /**< 调用者的环形缓冲区；Platform 只借用。 */
    uint32_t FramesPerHalf;               /**< 每半区帧数。 */
    uint32_t SampleRate;                  /**< 当前采样率。 */
    Platform_Audio_EventCallback_t Callback; /**< ISR 中调用的单槽回调。 */
    void *CallbackContext;                /**< 原样传给回调的上下文。 */
} Platform_Audio_HandleTypeDef;

/** @brief 本板唯一音频输出实例。 */
static Platform_Audio_HandleTypeDef hplatform_audio;

/**
  * @brief 本板 Audio Device 使用的 STM32 HAL I2S 与静音 GPIO 装配上下文。
  * @note  Platform 拥有本装配关系，但只借用 CubeMX 创建的 hi2s2 和 GPIO
  *        Vendor 对象，不拥有这些对象本身的生命周期。
  */
static AudioI2S_STM32HALAdapterTypeDef hplatform_audio_adapter = {
    .I2SHandle = &hi2s2,
    .MutePort = PCM_XSMT_GPIO_Port,
    .MutePin = PCM_XSMT_Pin
};

/**
  * @brief  进入关中断临界区。
  * @return 进入前的 PRIMASK，供 platform_audio_exit_critical() 恢复。
  */
static uint32_t platform_audio_enter_critical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/**
  * @brief  退出关中断临界区。
  * @param  primask platform_audio_enter_critical() 返回的值。
  */
static void platform_audio_exit_critical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
  * @brief  判断采样率是否被本板支持。
  * @param  sample_rate 采样率，单位 Hz。
  * @retval true 32、44.1 或 48 kHz。
  */
static bool platform_audio_rate_supported(uint32_t sample_rate)
{
    return (sample_rate == 32000U) ||
           (sample_rate == 44100U) ||
           (sample_rate == 48000U);
}

/**
  * @brief  私有静音控制，只由本 Module 的 Start/Stop 编排。
  * @param  mute true 拉低 XSMT 静音，false 解除静音。
  * @retval PLATFORM_OK 静音状态已写入。
  * @retval PLATFORM_AUDIO_ERROR Audio Device 或静音后端失败。
  */
static Platform_StatusTypeDef platform_audio_mute(bool mute)
{
    if (Audio_Mute(&hplatform_audio.Device, mute) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "mute control failed: error=%d, bus=%d",
                         (int)hplatform_audio.Device.ErrorCode,
                         (int)hplatform_audio.Device.LastBusStatus);

        return PLATFORM_AUDIO_ERROR;
    }

    return PLATFORM_OK;
}

/**
  * @brief  接收 Adapter 的循环输出事件并累加完成事实。
  * @param  context 未使用，状态为文件内唯一实例。
  * @param  event 刚发生的循环输出事件。
  * @note   ISR 上下文：只累加计数、置故障并调用单槽回调。Stop 成功后 Running
  *         为假，迟到的事件被忽略，因此 Stop 返回 OK 后不再回调。
  */
static void platform_audio_stream_event(void *context, Audio_StreamEventTypeDef event)
{
    (void)context;

    Platform_Audio_HandleTypeDef *h = &hplatform_audio;

    if (!h->Running)
    {
        return;
    }

    switch (event)
    {
        case AUDIO_STREAM_EVENT_HALF0_RELEASED:
        case AUDIO_STREAM_EVENT_HALF1_RELEASED:
        {
            uint32_t released = (event == AUDIO_STREAM_EVENT_HALF0_RELEASED) ? 0U : 1U;
            uint32_t other = 1U - released;

            h->Released++;

            if (!h->Submitted[other])
            {
                h->Underruns++;
            }

            h->Submitted[released] = false;
            break;
        }

        case AUDIO_STREAM_EVENT_ERROR:
        default:
            if (h->Fault == PLATFORM_AUDIO_FAULT_NONE)
            {
                h->Fault = PLATFORM_AUDIO_FAULT_TRANSFER_ERROR;
            }
            break;
    }

    Platform_Audio_EventCallback_t callback = h->Callback;

    if (callback != NULL)
    {
        callback(h->CallbackContext);
    }
}

/**
  * @brief  中止输出并核实 DMA 流使能位已清除。
  * @retval true DMA 流已停止。
  * @retval false Abort 后 DMA 流仍使能（HAL 返回码不作判据）。
  */
static bool platform_audio_abort_and_verify(void)
{
    (void)Audio_Abort(&hplatform_audio.Device);
    return !Audio_IsStreamActive(&hplatform_audio.Device);
}

/**
  * @brief  绑定本板音频 Port 并初始化唯一的 Audio Device 实例。
  * @retval PLATFORM_OK 初始化完成，Audio Device 进入 READY 状态且已静音。
  * @retval PLATFORM_AUDIO_ERROR Port 绑定或 Audio Device 初始化失败。
  */
Platform_StatusTypeDef Platform_Audio_Init(void)
{
    if (AudioI2S_STM32HALAdapter_Bind(
            &hplatform_audio.Device,
            &hplatform_audio_adapter) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "AUDIO", "Audio port bind failed");
        return PLATFORM_AUDIO_ERROR;
    }

    Audio_StatusTypeDef audio_status = Audio_Init(&hplatform_audio.Device);

    if (audio_status != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "initialization failed: error=%d, bus=%d",
                         (int)hplatform_audio.Device.ErrorCode,
                         (int)hplatform_audio.Device.LastBusStatus);
        return PLATFORM_AUDIO_ERROR;
    }

    hplatform_audio.Running = false;
    hplatform_audio.Fault = PLATFORM_AUDIO_FAULT_NONE;
    hplatform_audio.Released = 0U;
    hplatform_audio.Underruns = 0U;

    if (Audio_SetStreamEventHandler(&hplatform_audio.Device,
                                    platform_audio_stream_event,
                                    NULL) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "AUDIO", "event handler install failed");
        return PLATFORM_AUDIO_ERROR;
    }

    return PLATFORM_OK;
}

/**
  * @brief  注册单槽事件回调。
  * @param  callback ISR 中调用的轻量唤醒函数，不能为 NULL。
  * @param  context 原样传给 callback 的上下文。
  * @retval PLATFORM_OK 回调已安装，覆盖旧回调。
  * @retval PLATFORM_AUDIO_ERROR callback 为 NULL。
  */
Platform_StatusTypeDef Platform_Audio_SetEventCallback(Platform_Audio_EventCallback_t callback,
                                                       void *context)
{
    if (callback == NULL)
    {
        return PLATFORM_AUDIO_ERROR;
    }

    uint32_t primask = platform_audio_enter_critical();
    hplatform_audio.Callback = callback;
    hplatform_audio.CallbackContext = context;
    platform_audio_exit_critical(primask);

    return PLATFORM_OK;
}

/**
  * @brief  清除单槽事件回调。
  * @retval PLATFORM_OK 回调已清除。
  */
Platform_StatusTypeDef Platform_Audio_ClearEventCallback(void)
{
    uint32_t primask = platform_audio_enter_critical();
    hplatform_audio.Callback = NULL;
    hplatform_audio.CallbackContext = NULL;
    platform_audio_exit_critical(primask);

    return PLATFORM_OK;
}

/**
  * @brief  启动 I2S2 循环 DMA 输出。
  * @param  sample_rate 采样率，仅支持 32000、44100、48000。
  * @param  ring 环形缓冲区，立体声交错 16 位 PCM，首地址按 D-Cache 行对齐。
  * @param  frames_per_half 每半区帧数，使半区字节数为 D-Cache 行的整数倍。
  * @retval PLATFORM_OK 已启动并解除静音；两个半区视为已提交。
  * @retval PLATFORM_AUDIO_ERROR 故障已锁存、采样率或缓冲区参数不被接受，或启动失败。
  *         调用者读 GetStatus：Fault 为 STOP_FAILED 即锁存，否则按参数拒绝处理。
  * @note   PLL3 不动，仅重新 Init I2S。阻塞时长约为 I2S 重新初始化加 DAC 就绪等待。
  */
Platform_StatusTypeDef Platform_Audio_Start(uint32_t sample_rate,
                                            int16_t *ring,
                                            uint32_t frames_per_half)
{
    Platform_Audio_HandleTypeDef *h = &hplatform_audio;

    if (h->Fault == PLATFORM_AUDIO_FAULT_STOP_FAILED)
    {
        return PLATFORM_AUDIO_ERROR;
    }

    if (h->Running ||
        !platform_audio_rate_supported(sample_rate) ||
        (ring == NULL) ||
        (frames_per_half == 0U) ||
        (frames_per_half > (PLATFORM_AUDIO_MAX_ITEMS /
                            (PLATFORM_AUDIO_ITEMS_PER_FRAME * PLATFORM_AUDIO_HALF_COUNT))))
    {
        return PLATFORM_AUDIO_ERROR;
    }

    const uint32_t half_bytes = frames_per_half * PLATFORM_AUDIO_ITEMS_PER_FRAME * (uint32_t)sizeof(int16_t);

    if ((((uintptr_t)ring % PLATFORM_DCACHE_LINE_SIZE) != 0U) ||
        ((half_bytes % PLATFORM_DCACHE_LINE_SIZE) != 0U))
    {
        return PLATFORM_AUDIO_ERROR;
    }

    if (platform_audio_mute(true) != PLATFORM_OK)
    {
        return PLATFORM_AUDIO_ERROR;
    }

    if (Audio_Configure(&h->Device, sample_rate) != AUDIO_OK)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "configure failed: error=%d, bus=%d",
                         (int)h->Device.ErrorCode,
                         (int)h->Device.LastBusStatus);
        return PLATFORM_AUDIO_ERROR;
    }

    if (!CortexM7DCache_Clean_Aligned(ring, half_bytes * PLATFORM_AUDIO_HALF_COUNT))
    {
        return PLATFORM_AUDIO_ERROR;
    }

    uint32_t primask = platform_audio_enter_critical();
    h->Released = 0U;
    h->Underruns = 0U;
    h->Fault = PLATFORM_AUDIO_FAULT_NONE;
    h->Submitted[0] = true;
    h->Submitted[1] = true;
    h->Ring = ring;
    h->FramesPerHalf = frames_per_half;
    h->SampleRate = sample_rate;
    h->Running = true;
    platform_audio_exit_critical(primask);

    if (Audio_StartCircular(&h->Device,
                            ring,
                            frames_per_half * PLATFORM_AUDIO_ITEMS_PER_FRAME * PLATFORM_AUDIO_HALF_COUNT) != AUDIO_OK)
    {
        h->Running = false;
        (void)LOG_Printf(LOG_LEVEL_ERROR,
                         "AUDIO",
                         "start failed: error=%d, bus=%d",
                         (int)h->Device.ErrorCode,
                         (int)h->Device.LastBusStatus);
        return PLATFORM_AUDIO_ERROR;
    }

    HAL_Delay(PLATFORM_AUDIO_DAC_READY_MS);

    if (platform_audio_mute(false) != PLATFORM_OK)
    {
        (void)Platform_Audio_Stop();
        return PLATFORM_AUDIO_ERROR;
    }

    return PLATFORM_OK;
}

/**
  * @brief  Clean 一个半区并标记为已提交。
  * @param  half 半区号，0 或 1。
  * @retval PLATFORM_OK 已提交。
  * @retval PLATFORM_AUDIO_ERROR 输出未运行、半区号非法或 Cache 维护失败。
  */
Platform_StatusTypeDef Platform_Audio_SubmitHalf(uint32_t half)
{
    Platform_Audio_HandleTypeDef *h = &hplatform_audio;

    if ((half >= PLATFORM_AUDIO_HALF_COUNT) || !h->Running)
    {
        return PLATFORM_AUDIO_ERROR;
    }

    const uint32_t half_items = h->FramesPerHalf * PLATFORM_AUDIO_ITEMS_PER_FRAME;
    const int16_t *half_start = &h->Ring[half * half_items];

    if (!CortexM7DCache_Clean_Aligned(half_start, half_items * (uint32_t)sizeof(int16_t)))
    {
        return PLATFORM_AUDIO_ERROR;
    }

    uint32_t primask = platform_audio_enter_critical();
    h->Submitted[half] = true;
    platform_audio_exit_critical(primask);

    return PLATFORM_OK;
}

/**
  * @brief  读取输出完成事实快照。
  * @param  status 输出：快照；Stop 成功后保持最后一次 Start 的冻结值。
  * @retval PLATFORM_OK 快照已写入。
  * @retval PLATFORM_AUDIO_ERROR status 为 NULL。
  */
Platform_StatusTypeDef Platform_Audio_GetStatus(Platform_Audio_StatusTypeDef *status)
{
    if (status == NULL)
    {
        return PLATFORM_AUDIO_ERROR;
    }

    uint32_t primask = platform_audio_enter_critical();
    status->ReleasedHalves = hplatform_audio.Released;
    status->Underruns = hplatform_audio.Underruns;
    status->Fault = hplatform_audio.Fault;
    platform_audio_exit_critical(primask);

    return PLATFORM_OK;
}

/**
  * @brief  在调用者上下文同步停止输出。
  * @retval PLATFORM_OK 输出已停止（含本来就未运行）；之后不再回调，缓冲交还调用者。
  * @retval PLATFORM_AUDIO_ERROR DMA 流重试一次后仍未停止，已锁存 STOP_FAILED；
  *         缓冲留在 Platform，之后 Start 一律拒绝直到重启。
  * @note   流程：软静音并等斜坡，Abort，核实 DMA 流 EN 位为 0（HAL 返回码不作判据）；
  *         失败时重新 Init I2S 与 DMA 后重试一次。最坏阻塞为斜坡加两次 HAL Abort
  *         超时（各约 5 ms）。
  */
Platform_StatusTypeDef Platform_Audio_Stop(void)
{
    Platform_Audio_HandleTypeDef *h = &hplatform_audio;

    if (h->Fault == PLATFORM_AUDIO_FAULT_STOP_FAILED)
    {
        return PLATFORM_AUDIO_ERROR;
    }

    if (!h->Running)
    {
        return PLATFORM_OK;
    }

    (void)platform_audio_mute(true);
    HAL_Delay(PLATFORM_AUDIO_MUTE_RAMP_MS);

    bool stopped = platform_audio_abort_and_verify();

    if (!stopped)
    {
        (void)Audio_Configure(&h->Device, h->SampleRate);
        stopped = platform_audio_abort_and_verify();
    }

    uint32_t primask = platform_audio_enter_critical();
    h->Running = false;
    if (!stopped)
    {
        h->Fault = PLATFORM_AUDIO_FAULT_STOP_FAILED;
    }
    platform_audio_exit_critical(primask);

    if (!stopped)
    {
        (void)LOG_Printf(LOG_LEVEL_ERROR, "AUDIO", "stop failed: DMA stream still enabled, latched");
        return PLATFORM_AUDIO_ERROR;
    }

    return PLATFORM_OK;
}
