#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** @brief  Audio status enumeration */
typedef enum
{
    AUDIO_OK = 0,
    AUDIO_ERROR,
} Audio_StatusTypeDef;

typedef enum
{
    AUDIO_STATE_RESET = 0,
    AUDIO_STATE_READY,
    AUDIO_STATE_BUSY,
    AUDIO_STATE_ERROR
} Audio_StateTypeDef;

typedef enum
{
    AUDIO_BUS_OK = 0u, /**< 总线操作成功。 */
    AUDIO_BUS_ERROR,   /**< 未进一步分类的总线错误。 */
    AUDIO_BUS_BUSY,    /**< 总线或底层句柄正忙。 */
    AUDIO_BUS_TIMEOUT  /**< 总线操作超时。 */
} Audio_BusStatusTypeDef;

/**
  * @brief Audio Device 层可理解的最近一次错误原因。
  * @note  ErrorCode 描述失败阶段；底层 BUSY/TIMEOUT 等原因由
  *        LastBusStatus 补充。二者都不暴露 HAL 原始错误位。
  */
typedef enum
{
    AUDIO_ERROR_NONE = 0u,       /**< 没有错误。 */
    AUDIO_ERROR_INVALID_PARAM,   /**< 句柄、缓冲区或长度参数非法。 */
    AUDIO_ERROR_PORT_NOT_BOUND,  /**< Bus Ops、Context 或静音回调未绑定。 */
    AUDIO_ERROR_NOT_READY,       /**< 当前生命周期状态不允许执行该操作。 */
    AUDIO_ERROR_BUS_PREPARE,     /**< 音频总线准备失败。 */
    AUDIO_ERROR_BUS_TRANSMIT,    /**< 音频数据发送失败。 */
    AUDIO_ERROR_MUTE             /**< 静音或解除静音操作失败。 */
} Audio_ErrorTypeDef;

typedef Audio_BusStatusTypeDef (*Audio_BusTransmitFunc)(void *BusContext, const uint16_t *data, uint16_t size);
typedef Audio_BusStatusTypeDef (*Audio_BusPrepareFunc)(void *BusContext);

typedef struct
{
    Audio_BusTransmitFunc Transmit;
    Audio_BusPrepareFunc Prepare;
} Audio_BusOpsTypeDef;

typedef Audio_StatusTypeDef (*Audio_MuteFunc)(void *MuteContext, bool mute);

typedef struct
{
    // Add audio configuration parameters here
    const Audio_BusOpsTypeDef *BusOps;
    void *BusContext; // Pointer to the underlying bus context (e.g., I2S handle)

    Audio_MuteFunc Mute;
    void *MuteContext; //GPIO控制可以不写

    volatile Audio_StateTypeDef State;
    volatile Audio_ErrorTypeDef ErrorCode;
    volatile Audio_BusStatusTypeDef LastBusStatus;
    bool IsMuted;
} Audio_HandleTypeDef;

Audio_StatusTypeDef Audio_Init(Audio_HandleTypeDef *haudio);
Audio_StatusTypeDef Audio_Transmit(Audio_HandleTypeDef *haudio, const uint16_t *data, uint16_t size);
Audio_StatusTypeDef Audio_Mute(Audio_HandleTypeDef *haudio, bool mute);

#endif
