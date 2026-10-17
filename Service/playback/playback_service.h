/**
  ******************************************************************************
  * @file    playback_service.h
  * @brief   播放模块 Interface：音频帧字节 → PCM 帧 → Platform 音频输出。
  ******************************************************************************
  */

#ifndef PLAYBACK_SERVICE_H
#define PLAYBACK_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "Service/service.h"
#include "Service/playback/playback_service_config.h"

/** @brief 一次推进的结果；播放模块只回报事实，不认识曲目、序号、文件或块。 */
typedef enum
{
    SERVICE_PLAYBACK_ADVANCE_NEED_INPUT = 0U, /**< 窗口字节数低于 W_min 且输入未结束，未解码；请交入更多字节。 */
    SERVICE_PLAYBACK_ADVANCE_FRAME,           /**< 解出一帧 PCM，已入帧输出缓冲，用 Service_Playback_ReadFrame() 取走。 */
    SERVICE_PLAYBACK_ADVANCE_SKIPPED,         /**< 消耗了字节但没有 PCM：失步、被丢弃的帧或 Xing/VBRI 元数据帧；可立即再推进。 */
    SERVICE_PLAYBACK_ADVANCE_SYNC_EXHAUSTED,  /**< 同步预算 R 耗尽，当前流不可解；直到重新开始新流前保持此结果。 */
    SERVICE_PLAYBACK_ADVANCE_END              /**< 输入已结束且窗口已空；截断尾部残余已静默丢弃。 */
} Service_Playback_AdvanceResultTypeDef;

/** @brief 一条流的首帧信息；首帧解出的那次推进里随报告一并给出。 */
typedef struct
{
    uint32_t SampleRateHz;     /**< 采样率，本流此后与它不同的帧被丢弃。 */
    uint8_t  Channels;         /**< 声道数（1 或 2）；ReadFrame 总是输出立体声，单声道复制到左右。 */
    uint32_t TotalDurationMs;  /**< 总时长；0 表示未知。 */
    bool     DurationFromHeader; /**< 总时长来自 Xing/VBRI 的帧数（真）还是按音频字节数与比特率估算（假）。 */
} Service_Playback_StreamInfoTypeDef;

/** @brief Service_Playback_Advance() 回报的事实。 */
typedef struct
{
    Service_Playback_AdvanceResultTypeDef Result; /**< 本次推进的结果。 */
    bool     FirstFrame;                          /**< 本次推进解出了本流的首帧，StreamInfo 有效。 */
    Service_Playback_StreamInfoTypeDef StreamInfo; /**< 首帧信息；FirstFrame 为假时保持上次的值（首帧之前为全 0）。 */
    uint32_t PcmFrames;                           /**< Result 为 FRAME 时帧输出缓冲里的立体声帧数（每声道采样数）。 */
    uint32_t DecodedMs;                           /**< 已解码时间：按已解出的每声道采样数与采样率累计，越过总时长时钳在总时长。 */
} Service_Playback_AdvanceReportTypeDef;

/**
 * @brief Service_Playback_Pump() 回报的事实；播放模块只回报事实，不认识曲目、序号、文件或块。
 * @note  事件类（FirstFrame、NeedInput、OutputStarted、OutputProgress、Underruns）只在发生的那次推进里给出；
 *        状态类（SyncExhausted、StreamEnded、StartRejected、TransferError、StopFailed）在条件持续时每次推进重复给出，
 *        直到新流开始或模块重新初始化。
 */
typedef struct
{
    bool     FirstFrame;          /**< 本次推进解出了本流的首帧，StreamInfo 有效（暂停中的预填也会给出）。 */
    Service_Playback_StreamInfoTypeDef StreamInfo; /**< 首帧信息；首帧之前为全 0。 */
    uint32_t DecodedMs;           /**< 已解码时间：只增不减，欠载恢复、暂停与同曲恢复的重放都不让它回退。 */
    bool     NeedInput;           /**< 补半区时输入不够（窗口低于 W_min 且输入未结束）；请交入更多字节后再推进。 */
    bool     SyncExhausted;       /**< 本流不可播：同步预算 R 耗尽，或输入读尽仍一帧都没解出。 */
    bool     OutputStarted;       /**< 本次推进成功 Start 了输出（含欠载恢复后的重新 Start）；T_out 以此起算。 */
    bool     OutputProgress;      /**< 自上次推进起 Platform 又释放了半区，即输出有进展（供 T_out）。 */
    uint32_t Underruns;           /**< 本次推进处理的欠载次数；已按 Stop、补满、Start 恢复，对外不变，只供调用者记日志。 */
    bool     StreamEnded;         /**< 流尾放完：最后一个含真实数据的半区已释放，输出已 Stop。 */
    bool     StartRejected;       /**< Platform 因参数（如不支持的采样率）拒绝 Start，未锁存；本流应跳过。 */
    bool     TransferError;       /**< Platform 报告 TRANSFER_ERROR；调用者应放弃当前流（其中 Stop 输出）。 */
    bool     StopFailed;          /**< Platform 锁存了 STOP_FAILED；此后模块不再写 PCM 半区，重启前不能出声。 */
} Service_Playback_PumpReportTypeDef;

Service_StatusTypeDef Service_Playback_Init(void);
Service_StatusTypeDef Service_Playback_BeginStream(uint32_t audio_bytes);
Service_StatusTypeDef Service_Playback_Feed(const uint8_t *data, uint32_t length, uint32_t *accepted);
Service_StatusTypeDef Service_Playback_FinishInput(void);
Service_StatusTypeDef Service_Playback_Advance(Service_Playback_AdvanceReportTypeDef *report);
Service_StatusTypeDef Service_Playback_ReadFrame(int16_t *stereo_pcm, uint32_t capacity_frames, uint32_t *frames);
Service_StatusTypeDef Service_Playback_Pump(Service_Playback_PumpReportTypeDef *report);
Service_StatusTypeDef Service_Playback_PauseOutput(void);
Service_StatusTypeDef Service_Playback_ResumeOutput(void);
Service_StatusTypeDef Service_Playback_AbandonStream(void);

#endif /* PLAYBACK_SERVICE_H */
