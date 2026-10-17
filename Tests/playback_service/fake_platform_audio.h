/**
 * @file fake_platform_audio.h
 * @brief 缝 2 的假 Platform 音频：实现 platform_audio.h 的全部公开函数，记录调用并可注入事实。
 * @note  假对象只实现既有公开接口，不向生产头增加入口；它按 platform_audio.h 与 platform_audio.c 的
 *        合同建模（E1–E5）：采样率只收 32/44.1/48 kHz，缓冲区首地址与半区字节数须按 32 B 对齐，
 *        运行中再 Start 被拒，Start 清零计数，Stop 成功后冻结计数，STOP_FAILED 锁存后 Start/Stop 一律拒绝，
 *        半区释放时另一半区未提交即计一次欠载。测试里的注入函数是 fake_audio_*，不在生产头里。
 */

#ifndef FAKE_PLATFORM_AUDIO_H
#define FAKE_PLATFORM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "Platform/audio/platform_audio.h"

/** @brief 假对象记录的调用种类。 */
typedef enum
{
    FAKE_AUDIO_CALL_START = 0,
    FAKE_AUDIO_CALL_SUBMIT,
    FAKE_AUDIO_CALL_STOP
} fake_audio_call_kind_t;

/** @brief 一条调用记录。 */
typedef struct
{
    fake_audio_call_kind_t Kind;
    uint32_t Arg; /**< Start 为采样率，SubmitHalf 为半区号，Stop 为 0。 */
} fake_audio_call_t;

#define FAKE_AUDIO_MAX_CALLS (4096U)

/** @brief 假对象的可观察状态；测试只读。 */
typedef struct
{
    fake_audio_call_t Calls[FAKE_AUDIO_MAX_CALLS];
    uint32_t CallCount;
    uint32_t StartCount;
    uint32_t SubmitCount;
    uint32_t StopCount;
    uint32_t StartFrames;              /**< 最近一次成功 Start 的 frames_per_half。 */
    uint32_t StartRate;                /**< 最近一次成功 Start 的采样率。 */
    const int16_t *StartRing;          /**< 最近一次成功 Start 的 ring。 */
    int16_t *StartSnapshot;            /**< 最近一次成功 Start 时两个半区内容的副本（2 × 半区样本数）。 */
    uint32_t OwnershipViolations;      /**< SubmitHalf 提交了仍处于已提交态（DMA 持有）的半区的次数；应为 0。 */
    uint32_t ContractViolations;       /**< 违反 platform_audio.h 合同的调用（如运行中再 Start、未运行 SubmitHalf）。 */
    int Running;
} fake_audio_state_t;

extern fake_audio_state_t g_fake_audio;

/** @brief 复位假对象到上电后状态（清记录、清锁存、清播放日志、恢复接受规则）。 */
void fake_audio_reset(void);

/** @brief 取回已播放日志：每次半区被 DMA 读完时追加该半区的内容（立体声交错 int16）。 */
const int16_t *fake_audio_played(size_t *samples);

/**
 * @brief 模拟 DMA 读完一个半区：释放计数加一，另一半区未提交则欠载加一，随后调用已注册的事件回调。
 * @return 1 表示发生了释放；输出未运行（含 Stop 之后）时不释放，返回 0。
 */
int fake_audio_release_half(void);

/** @brief 注入 DMA 传输错误：记 TRANSFER_ERROR 并调用回调；输出仍视为运行，等待调用者 Stop。 */
void fake_audio_inject_transfer_error(void);

/** @brief 让下一次 Stop 失败：锁存 STOP_FAILED 并返回错误（缓冲留在 Platform）。 */
void fake_audio_fail_next_stop(void);

/** @brief 直接锁存 STOP_FAILED（模拟 Stop 之外的时机已锁存），之后 Start/Stop 一律拒绝。 */
void fake_audio_latch_stop_failed(void);

/** @brief 让下一次 Start 因参数被拒（不锁存，Fault 保持不变）。 */
void fake_audio_reject_next_start(void);

/**
 * @brief 额外接受一个采样率（默认只收 32000、44100、48000）。
 * @param[in] rate 采样率。
 * @note  仅用于用 MPEG-2 夹具构造“非整半区”的流尾；真实 Platform 仍拒绝它，生产合同不变。
 */
void fake_audio_accept_rate_for_test(uint32_t rate);

/** @brief 取回某半区最近一次 Clean 出去（Start 或 SubmitHalf）的内容，即 DMA 将读到的数据。 */
const int16_t *fake_audio_clean_half(uint32_t half);

/** @brief 事件回调被调用的次数（ISR 唤醒的模拟）。 */
uint32_t fake_audio_callback_count(void);

#endif /* FAKE_PLATFORM_AUDIO_H */
