/**
  ******************************************************************************
  * @file    sd.h
  * @brief   可复用 SD 卡块设备驱动的公共接口。
  *
  * @details
  *          本模块通过 SDCard_PortOpsTypeDef 使用底层控制器，不直接依赖
  *          STM32 HAL、SDMMC 实例或卡检测 GPIO。驱动负责介质状态、容量
  *          信息缓存、块范围校验、同步等待和错误诊断；Platform 层负责绑定
  *          当前 PCB 对应的 Port Adapter。
  *
  *          Status 只描述单次调用结果，State 描述持续生命周期。无卡时 Init
  *          可以成功并进入 NOT_PRESENT；DeInit 可重复调用。Refresh 不负责
  *          卡检测消抖且不得从 ISR 调用。
  ******************************************************************************
  */

#ifndef SD_H
#define SD_H

/* Includes ------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/
/**
  * @brief SD Card Device 函数的立即返回状态。
  * @note  返回值只描述本次调用是否成功；介质的持续状态由
  *        SDCard_StateTypeDef 表示。
  */
typedef enum
{
    SDCARD_OK = 0u, /**< 本次操作成功。 */
    SDCARD_ERROR    /**< 本次操作失败，详细原因保存在句柄中。 */
} SDCard_StatusTypeDef;

/**
  * @brief SD 卡实例的持续运行状态。
  */
typedef enum
{
    SDCARD_STATE_RESET = 0u,  /**< 尚未初始化或已经显式反初始化。 */
    SDCARD_STATE_NOT_PRESENT, /**< 卡槽中没有检测到介质。 */
    SDCARD_STATE_READY,       /**< 介质已初始化，可以进行块访问。 */
    SDCARD_STATE_BUSY,        /**< 正在执行初始化、同步或块传输。 */
    SDCARD_STATE_ERROR        /**< 最近一次底层操作使设备进入错误状态。 */
} SDCard_StateTypeDef;

/**
  * @brief SD Card Device 层错误原因。
  */
typedef enum
{
    SDCARD_ERROR_NONE = 0u,       /**< 无错误。 */
    SDCARD_ERROR_INVALID_PARAM,   /**< 句柄、缓冲区、块数量等参数非法。 */
    SDCARD_ERROR_PORT_NOT_BOUND,  /**< Port Ops 或 Port Context 尚未绑定。 */
    SDCARD_ERROR_NOT_PRESENT,     /**< 当前没有检测到 SD 卡。 */
    SDCARD_ERROR_NOT_READY,       /**< 设备当前状态不允许执行该操作。 */
    SDCARD_ERROR_OUT_OF_RANGE,    /**< 请求的块范围超出介质容量。 */
    SDCARD_ERROR_INVALID_INFO,    /**< 底层返回的块数量或块大小非法。 */
    SDCARD_ERROR_PORT_INIT,       /**< 底层控制器或 SD 卡初始化失败。 */
    SDCARD_ERROR_PORT_DEINIT,     /**< 底层反初始化失败。 */
    SDCARD_ERROR_PORT_GET_INFO,   /**< 获取介质信息失败。 */
    SDCARD_ERROR_PORT_READ,       /**< 读取块失败。 */
    SDCARD_ERROR_PORT_WRITE,      /**< 写入块失败。 */
    SDCARD_ERROR_PORT_SYNC        /**< 等待介质完成内部传输失败。 */
} SDCard_ErrorTypeDef;

/**
  * @brief Device 层可理解的统一 Port 状态。
  * @note  HAL SDMMC、SPI SD 或测试 Adapter 的原始状态必须转换为此枚举。
  */
typedef enum
{
    SDCARD_PORT_OK = 0u,   /**< 底层操作成功。 */
    SDCARD_PORT_ERROR,     /**< 未进一步分类的底层错误。 */
    SDCARD_PORT_BUSY,      /**< 控制器或介质正忙。 */
    SDCARD_PORT_TIMEOUT,   /**< 底层操作超时。 */
    SDCARD_PORT_NOT_PRESENT /**< 操作期间检测到介质不存在。 */
} SDCard_PortStatusTypeDef;

/**
  * @brief 经过归一化的 SD 卡块设备信息。
  */
typedef struct
{
    uint64_t CapacityBytes; /**< 逻辑容量，等于 BlockCount * BlockSize。 */
    uint32_t BlockCount;    /**< 可访问的逻辑块总数。 */
    uint32_t BlockSize;     /**< 单个逻辑块的字节数，SD 通常为 512。 */
    uint32_t CardType;      /**< Adapter 提供的卡类型标识，仅用于诊断。 */
    uint32_t CardVersion;   /**< Adapter 提供的卡版本标识，仅用于诊断。 */
} SDCard_InfoTypeDef;

/**
  * @brief 判断卡槽中是否存在介质的 Port 函数类型。
  * @param context 与 PortOps 成对绑定的底层控制器和卡检测对象。
  * @retval true 当前检测到介质。
  * @retval false 当前没有检测到介质。
  */
typedef bool (*SDCard_PortIsPresentFunc)(const void *context);

/**
  * @brief 初始化底层控制器和介质的 Port 函数类型。
  * @param context 与 PortOps 成对绑定的底层对象。
  * @retval SDCard_PortStatusTypeDef 归一化后的初始化结果。
  */
typedef SDCard_PortStatusTypeDef (*SDCard_PortInitFunc)(void *context);

/**
  * @brief 反初始化底层控制器的 Port 函数类型。
  * @param context 与 PortOps 成对绑定的底层对象。
  * @retval SDCard_PortStatusTypeDef 归一化后的资源释放结果。
  */
typedef SDCard_PortStatusTypeDef (*SDCard_PortDeInitFunc)(void *context);

/**
  * @brief 获取归一化介质信息的 Port 函数类型。
  * @param context 与 PortOps 成对绑定的底层对象。
  * @param info 接收容量、逻辑块和诊断信息的对象。
  * @retval SDCard_PortStatusTypeDef 归一化后的查询结果。
  */
typedef SDCard_PortStatusTypeDef (*SDCard_PortGetInfoFunc)(void *context,
    SDCard_InfoTypeDef *info);

/**
  * @brief 从连续逻辑块读取数据的 Port 函数类型。
  * @param context 与 PortOps 成对绑定的底层对象。
  * @param data 接收块数据的缓冲区。
  * @param start_block 第一个逻辑块编号。
  * @param block_count 连续读取的逻辑块数量。
  * @param timeout_ms 底层数据阶段允许的最长时间。
  */
typedef SDCard_PortStatusTypeDef (*SDCard_PortReadBlocksFunc)(void *context,
    uint8_t *data,
    uint32_t start_block,
    uint32_t block_count,
    uint32_t timeout_ms);

/**
  * @brief 向连续逻辑块写入数据的 Port 函数类型。
  * @param context 与 PortOps 成对绑定的底层对象。
  * @param data 提供块数据的只读缓冲区。
  * @param start_block 第一个逻辑块编号。
  * @param block_count 连续写入的逻辑块数量。
  * @param timeout_ms 底层数据阶段允许的最长时间。
  */
typedef SDCard_PortStatusTypeDef (*SDCard_PortWriteBlocksFunc)(void *context,
    const uint8_t *data,
    uint32_t start_block,
    uint32_t block_count,
    uint32_t timeout_ms);

/**
  * @brief 启动非阻塞块读取的 Port 函数类型。
  * @note  返回 SDCARD_PORT_OK 只表示控制器已接受 DMA 请求；完成或失败通过
  *        上层注册的 IRQ 事件报告，随后由普通任务调用 SDCard_CompleteTransfer()
  *        或 SDCard_FailTransfer() 推进 Device 状态机。
  */
typedef SDCard_PortStatusTypeDef (*SDCard_PortStartReadBlocksFunc)(void *context,
    uint8_t *data,
    uint32_t start_block,
    uint32_t block_count);

/**
  * @brief 启动非阻塞块写入的 Port 函数类型。
  * @note  data 在传输完成事件到达前必须保持有效且不得被调用者修改。
  */
typedef SDCard_PortStatusTypeDef (*SDCard_PortStartWriteBlocksFunc)(void *context,
    const uint8_t *data,
    uint32_t start_block,
    uint32_t block_count);

/**
  * @brief 等待介质完成内部操作并回到可传输状态的 Port 函数类型。
  * @param context 与 PortOps 成对绑定的底层对象。
  * @param timeout_ms 最长等待时间。
  * @retval SDCard_PortStatusTypeDef 归一化后的同步结果。
  */
typedef SDCard_PortStatusTypeDef (*SDCard_PortSyncFunc)(void *context,
    uint32_t timeout_ms);

/**
  * @brief SD Card Device 的 Port 操作表。
  * @note  Ops 与 PortContext 必须由同一个 Adapter 成对安装。
  */
typedef struct
{
    SDCard_PortIsPresentFunc IsPresent;   /**< 读取介质检测状态。 */
    SDCard_PortInitFunc Init;             /**< 初始化控制器和 SD 卡。 */
    SDCard_PortDeInitFunc DeInit;         /**< 释放控制器资源。 */
    SDCard_PortGetInfoFunc GetInfo;       /**< 取得卡容量和逻辑块信息。 */
    SDCard_PortReadBlocksFunc ReadBlocks; /**< 同步发起块读取。 */
    SDCard_PortWriteBlocksFunc WriteBlocks; /**< 同步发起块写入。 */
    SDCard_PortStartReadBlocksFunc StartReadBlocks; /**< 启动异步块读取，可选。 */
    SDCard_PortStartWriteBlocksFunc StartWriteBlocks; /**< 启动异步块写入，可选。 */
    SDCard_PortSyncFunc Sync;             /**< 等待卡回到传输状态。 */
} SDCard_PortOpsTypeDef;

/**
  * @brief SD 卡设备实例句柄。
  * @note  PortOps 和 PortContext 是依赖；其余字段由 Device Implementation
  *        维护。Platform 可以私有持有本结构体，但应用不应直接伪造状态。
  */
typedef struct
{
    const SDCard_PortOpsTypeDef *PortOps; /**< 当前底层 Adapter 的操作表。 */
    void *PortContext;                    /**< Adapter 私有上下文，相当于 this。 */
    volatile SDCard_StateTypeDef State;   /**< 当前持续状态。 */
    volatile SDCard_ErrorTypeDef ErrorCode; /**< 最近一次 Device 层错误。 */
    volatile SDCard_PortStatusTypeDef LastPortStatus; /**< 最近一次 Port 状态。 */
    SDCard_InfoTypeDef Info;              /**< 初始化成功后缓存的介质信息。 */
    bool IsInfoValid;                     /**< Info 是否可向调用者复制。 */
    bool IsPortInitialized;               /**< Adapter 是否已经成功初始化。 */
} SDCard_HandleTypeDef;

/* Exported functions --------------------------------------------------------*/
SDCard_StatusTypeDef SDCard_Init(SDCard_HandleTypeDef *hsdcard);

SDCard_StatusTypeDef SDCard_DeInit(SDCard_HandleTypeDef *hsdcard);

SDCard_StatusTypeDef SDCard_Refresh(SDCard_HandleTypeDef *hsdcard);

SDCard_StatusTypeDef SDCard_ReadBlocks(SDCard_HandleTypeDef *hsdcard,
                                       uint8_t *data,
                                       uint32_t start_block,
                                       uint32_t block_count);

SDCard_StatusTypeDef SDCard_WriteBlocks(SDCard_HandleTypeDef *hsdcard,
                                        const uint8_t *data,
                                        uint32_t start_block,
                                        uint32_t block_count);

SDCard_StatusTypeDef SDCard_StartReadBlocks(SDCard_HandleTypeDef *hsdcard,
                                            uint8_t *data,
                                            uint32_t start_block,
                                            uint32_t block_count);

SDCard_StatusTypeDef SDCard_StartWriteBlocks(SDCard_HandleTypeDef *hsdcard,
                                             const uint8_t *data,
                                             uint32_t start_block,
                                             uint32_t block_count);

SDCard_StatusTypeDef SDCard_CompleteTransfer(SDCard_HandleTypeDef *hsdcard);

SDCard_StatusTypeDef SDCard_FailTransfer(SDCard_HandleTypeDef *hsdcard);

SDCard_StatusTypeDef SDCard_Sync(SDCard_HandleTypeDef *hsdcard);

SDCard_StatusTypeDef SDCard_GetInfo(const SDCard_HandleTypeDef *hsdcard,
                                    SDCard_InfoTypeDef *info);

SDCard_StateTypeDef SDCard_GetState(const SDCard_HandleTypeDef *hsdcard);

bool SDCard_IsPresent(const SDCard_HandleTypeDef *hsdcard);

#ifdef __cplusplus
}
#endif

#endif /* SD_H */
