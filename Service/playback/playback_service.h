/**
  ******************************************************************************
  * @file    playback_service.h
  * @brief   播放模块 Interface：音频帧字节 → PCM 帧（解码半边）。
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

Service_StatusTypeDef Service_Playback_Init(void);
Service_StatusTypeDef Service_Playback_BeginStream(uint32_t audio_bytes);
Service_StatusTypeDef Service_Playback_Feed(const uint8_t *data, uint32_t length, uint32_t *accepted);
Service_StatusTypeDef Service_Playback_FinishInput(void);
Service_StatusTypeDef Service_Playback_Advance(Service_Playback_AdvanceReportTypeDef *report);
Service_StatusTypeDef Service_Playback_ReadFrame(int16_t *stereo_pcm, uint32_t capacity_frames, uint32_t *frames);

#endif /* PLAYBACK_SERVICE_H */
