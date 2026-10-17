/**
  ******************************************************************************
  * @file    audio.h
  * @brief   可复用 Audio Device 的公共类型和接口。
  *
  * @details
  *          Device 只通过 Audio_BusOpsTypeDef 和 Audio_MuteFunc 使用底层
  *          I2S 循环 DMA 输出与静音控制，不直接依赖 STM32 HAL。Port 必须把
  *          函数表和对应 Context 成对安装到 Handle，随后才能调用 Audio_Init()。
  ******************************************************************************
  */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Exported types ------------------------------------------------------------*/
/** @brief Audio Device 函数的立即返回状态。 */
typedef enum
{
    AUDIO_OK = 0, /**< 本次操作成功。 */
    AUDIO_ERROR,  /**< 本次操作失败，详细原因保存在 Handle 中。 */
} Audio_StatusTypeDef;

/** @brief Audio Device 的持续生命周期状态。 */
typedef enum
{
    AUDIO_STATE_RESET = 0, /**< 尚未初始化。 */
    AUDIO_STATE_READY,     /**< 已初始化，可以执行控制或启动循环输出。 */
    AUDIO_STATE_BUSY,      /**< 正在执行同步总线操作。 */
    AUDIO_STATE_STREAMING, /**< 循环 DMA 输出已启动，尚未 Abort。 */
    AUDIO_STATE_ERROR      /**< 最近一次不可恢复操作失败。 */
} Audio_StateTypeDef;

/** @brief Audio Device 可理解的归一化总线状态。 */
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
    AUDIO_ERROR_BUS_CONFIGURE,   /**< 采样率配置（总线重新初始化）失败。 */
    AUDIO_ERROR_BUS_STREAM,      /**< 循环输出启动或中止失败。 */
    AUDIO_ERROR_MUTE             /**< 静音或解除静音操作失败。 */
} Audio_ErrorTypeDef;

/**
  * @brief 循环输出事件：由 Adapter 在 ISR 上下文报告。
  * @note  半区 0、半区 1 是循环缓冲区的前、后一半；事件表示 DMA 刚刚读完该半区。
  */
typedef enum
{
    AUDIO_STREAM_EVENT_HALF0_RELEASED = 0u, /**< DMA 读完半区 0。 */
    AUDIO_STREAM_EVENT_HALF1_RELEASED,      /**< DMA 读完半区 1。 */
    AUDIO_STREAM_EVENT_ERROR                /**< 总线或 DMA 报告传输错误。 */
} Audio_StreamEventTypeDef;

/**
  * @brief 循环输出事件处理函数类型。
  * @param context 注册时绑定的调用者上下文。
  * @param event 刚发生的循环输出事件。
  * @warning 在 ISR 上下文调用，只能执行常数时间、非阻塞且 FromISR 安全的操作。
  */
typedef void (*Audio_StreamEventFunc)(void *context, Audio_StreamEventTypeDef event);

/**
  * @brief 音频总线准备 Adapter 的函数类型。
  * @param BusContext 与 BusOps 成对绑定的底层音频后端对象。
  * @retval Audio_BusStatusTypeDef 后端可用性检查或准备结果。
  */
typedef Audio_BusStatusTypeDef (*Audio_BusPrepareFunc)(void *BusContext);

/**
  * @brief 按采样率重新初始化音频总线的 Adapter 函数类型。
  * @param BusContext 与 BusOps 成对绑定的底层音频后端对象。
  * @param sample_rate 目标采样率，单位 Hz。
  * @retval Audio_BusStatusTypeDef 重新初始化结果；成功后总线空闲。
  * @note   后端可借此恢复被中止后仍异常的 DMA 流。
  */
typedef Audio_BusStatusTypeDef (*Audio_BusConfigureFunc)(void *BusContext, uint32_t sample_rate);

/**
  * @brief 启动循环 DMA 输出的 Adapter 函数类型。
  * @param BusContext 与 BusOps 成对绑定的底层音频后端对象。
  * @param buffer 循环缓冲区，按 I2S 帧顺序排列的 16 位 PCM 数据。
  * @param item_count 整个循环缓冲区的 16 位数据单元数量，不是字节数。
  * @retval Audio_BusStatusTypeDef 启动结果。
  */
typedef Audio_BusStatusTypeDef (*Audio_BusStartCircularFunc)(void *BusContext, const int16_t *buffer, uint32_t item_count);

/**
  * @brief 中止循环输出的 Adapter 函数类型。
  * @param BusContext 与 BusOps 成对绑定的底层音频后端对象。
  * @retval Audio_BusStatusTypeDef 底层中止的返回码；调用者不得以它为唯一判据。
  */
typedef Audio_BusStatusTypeDef (*Audio_BusAbortFunc)(void *BusContext);

/**
  * @brief 查询循环输出的 DMA 流是否仍在运行的 Adapter 函数类型。
  * @param BusContext 与 BusOps 成对绑定的底层音频后端对象。
  * @retval true DMA 流仍使能；DMA 未配置时也返回 false。
  */
typedef bool (*Audio_BusIsStreamActiveFunc)(void *BusContext);

/**
  * @brief 安装循环输出事件处理函数的 Adapter 函数类型。
  * @param BusContext 与 BusOps 成对绑定的底层音频后端对象。
  * @param handler 事件处理函数，NULL 表示清除。
  * @param handler_context 原样传给 handler 的上下文。
  */
typedef void (*Audio_BusSetEventHandlerFunc)(void *BusContext, Audio_StreamEventFunc handler, void *handler_context);

/** @brief Audio Device 使用的总线操作表。 */
typedef struct
{
    Audio_BusPrepareFunc Prepare;                 /**< 检查或准备底层音频总线。 */
    Audio_BusConfigureFunc Configure;             /**< 按采样率重新初始化总线。 */
    Audio_BusStartCircularFunc StartCircular;     /**< 启动循环 DMA 输出。 */
    Audio_BusAbortFunc Abort;                     /**< 中止循环 DMA 输出。 */
    Audio_BusIsStreamActiveFunc IsStreamActive;   /**< 读取 DMA 流使能位。 */
    Audio_BusSetEventHandlerFunc SetEventHandler; /**< 安装 ISR 事件处理函数。 */
} Audio_BusOpsTypeDef;

/**
  * @brief 音频静音控制 Adapter 的函数类型。
  * @param MuteContext 与静音函数成对绑定的控制后端对象。
  * @param mute true 请求静音，false 请求解除静音。
  * @retval AUDIO_OK 控制请求已经执行。
  * @retval AUDIO_ERROR 参数或具体静音后端操作失败。
  */
typedef Audio_StatusTypeDef (*Audio_MuteFunc)(void *MuteContext, bool mute);

/**
  * @brief Audio Device Handle。
  * @note  BusOps/BusContext 和 Mute/MuteContext 必须分别成对绑定。
  */
typedef struct
{
    const Audio_BusOpsTypeDef *BusOps;       /**< 音频总线操作表。 */
    void *BusContext;                        /**< 传递给 BusOps 的底层总线上下文。 */
    Audio_MuteFunc Mute;                     /**< 静音控制函数。 */
    void *MuteContext;                       /**< 传递给 Mute 的静音控制上下文。 */
    volatile Audio_StateTypeDef State;       /**< 当前生命周期状态。 */
    volatile Audio_ErrorTypeDef ErrorCode;   /**< 最近一次 Device 层失败原因。 */
    volatile Audio_BusStatusTypeDef LastBusStatus; /**< 最近一次归一化总线状态。 */
    bool IsMuted;                            /**< Device 记录的当前静音状态。 */
} Audio_HandleTypeDef;

/* Exported functions --------------------------------------------------------*/
Audio_StatusTypeDef Audio_Init(Audio_HandleTypeDef *haudio);
Audio_StatusTypeDef Audio_Configure(Audio_HandleTypeDef *haudio, uint32_t sample_rate);
Audio_StatusTypeDef Audio_StartCircular(Audio_HandleTypeDef *haudio, const int16_t *buffer, uint32_t item_count);
Audio_StatusTypeDef Audio_Abort(Audio_HandleTypeDef *haudio);
bool Audio_IsStreamActive(const Audio_HandleTypeDef *haudio);
Audio_StatusTypeDef Audio_SetStreamEventHandler(Audio_HandleTypeDef *haudio, Audio_StreamEventFunc handler, void *handler_context);
Audio_StatusTypeDef Audio_Mute(Audio_HandleTypeDef *haudio, bool mute);

#endif /* AUDIO_H */
