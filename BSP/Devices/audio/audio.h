#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

/** @brief  Audio status enumeration */
typedef enum
{
    AUDIO_OK = 0,
    AUDIO_ERROR,
} Audio_StatusTypeDef;

typedef enum
{
    AUDIO_BUS_STATE_RESET = 0,
    AUDIO_BUS_STATE_READY,
    AUDIO_BUS_STATE_BUSY,
    AUDIO_BUS_STATE_ERROR
} Audio_StateTypeDef;

typedef enum
{
    AUDIO_BUS_OK = 0u, /**< 总线操作成功。 */
    AUDIO_BUS_ERROR,   /**< 未进一步分类的总线错误。 */
    AUDIO_BUS_BUSY,    /**< 总线或底层句柄正忙。 */
    AUDIO_BUS_TIMEOUT, /**< 总线操作超时。 */
    AUDIO_BUS_NACK     /**< 从机地址或数据未应答。 */
} Audio_BusStateTypeDef;


typedef Audio_BusStateTypeDef (*Audio_BusTransmitFunc)(void *BusContext, const uint16_t *data, uint16_t size);
typedef Audio_BusStateTypeDef (*Audio_BusPrepareFunc)(void *BusContext);

typedef struct
{
    Audio_BusTransmitFunc Transmit;
    Audio_BusPrepareFunc Prepare;
} Audio_BusOpsTypeDef;

typedef struct
{
    // Add audio configuration parameters here
    Audio_BusOpsTypeDef *BusOps;
    void *BusContext; // Pointer to the underlying bus context (e.g., I2S handle)
    Audio_StateTypeDef State;
} Audio_HandleTypeDef;

Audio_StatusTypeDef Audio_Init(Audio_HandleTypeDef *haudio);
Audio_StatusTypeDef Audio_Transmit(Audio_HandleTypeDef *haudio, const uint16_t *data, uint16_t size);

#endif