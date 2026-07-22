/**
  ******************************************************************************
  * @file    sd.h
  * @brief   可复用 SD 卡块设备驱动的公共接口。
  *
  * @details
  *          本模块通过 SDCard_PortOpsTypeDef 使用底层控制器，不直接依赖
  *          STM32 HAL、SDMMC 实例或卡检测 GPIO。驱动负责介质状态、容量
  *          信息缓存、块范围校验、同步等待和错误诊断；Board 层负责绑定
  *          当前 PCB 对应的 Port Adapter。
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
  * @brief Port 操作的归一化返回结果。
  */
typedef struct
{
    SDCard_PortStatusTypeDef Status; /**< 供 Device 控制流程使用的统一状态。 */
    uint32_t Detail;                 /**< Adapter 保存的底层原始错误码。 */
} SDCard_PortResultTypeDef;

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

/** @brief 判断卡槽中是否存在介质的 Port 函数类型。 */
typedef bool (*SDCard_PortIsPresentFunc)(const void *context);

/** @brief 初始化底层控制器和介质的 Port 函数类型。 */
typedef SDCard_PortResultTypeDef (*SDCard_PortInitFunc)(void *context);

/** @brief 反初始化底层控制器的 Port 函数类型。 */
typedef SDCard_PortResultTypeDef (*SDCard_PortDeInitFunc)(void *context);

/** @brief 获取归一化介质信息的 Port 函数类型。 */
typedef SDCard_PortResultTypeDef (*SDCard_PortGetInfoFunc)(
    void *context,
    SDCard_InfoTypeDef *info);

/** @brief 从连续逻辑块读取数据的 Port 函数类型。 */
typedef SDCard_PortResultTypeDef (*SDCard_PortReadBlocksFunc)(
    void *context,
    uint8_t *data,
    uint32_t start_block,
    uint32_t block_count,
    uint32_t timeout_ms);

/** @brief 向连续逻辑块写入数据的 Port 函数类型。 */
typedef SDCard_PortResultTypeDef (*SDCard_PortWriteBlocksFunc)(
    void *context,
    const uint8_t *data,
    uint32_t start_block,
    uint32_t block_count,
    uint32_t timeout_ms);

/** @brief 等待介质完成内部操作并回到可传输状态的 Port 函数类型。 */
typedef SDCard_PortResultTypeDef (*SDCard_PortSyncFunc)(
    void *context,
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
    SDCard_PortSyncFunc Sync;             /**< 等待卡回到传输状态。 */
} SDCard_PortOpsTypeDef;

/**
  * @brief SD 卡设备实例句柄。
  * @note  PortOps 和 PortContext 是依赖；其余字段由 Device Implementation
  *        维护。Board 可以私有持有本结构体，但应用不应直接伪造状态。
  */
typedef struct
{
    const SDCard_PortOpsTypeDef *PortOps; /**< 当前底层 Adapter 的操作表。 */
    void *PortContext;                    /**< Adapter 私有上下文，相当于 this。 */
    volatile SDCard_StateTypeDef State;   /**< 当前持续状态。 */
    volatile SDCard_ErrorTypeDef ErrorCode; /**< 最近一次 Device 层错误。 */
    volatile SDCard_PortStatusTypeDef LastPortStatus; /**< 最近一次 Port 状态。 */
    uint32_t PortErrorDetail;             /**< 最近一次底层原始错误码。 */
    SDCard_InfoTypeDef Info;              /**< 初始化成功后缓存的介质信息。 */
    bool IsInfoValid;                     /**< Info 是否可向调用者复制。 */
    bool IsPortInitialized;               /**< Adapter 是否已经成功初始化。 */
} SDCard_HandleTypeDef;

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  初始化 SD 卡并缓存介质信息。
  * @param  hsdcard 已安装 PortOps 和 PortContext 的 Device 句柄。
  * @retval SDCARD_OK 状态同步成功；无卡时 State 为 NOT_PRESENT。
  * @retval SDCARD_ERROR Port 未绑定、初始化失败或介质信息非法。
  */
SDCard_StatusTypeDef SDCard_Init(SDCard_HandleTypeDef *hsdcard);

/**
  * @brief  反初始化 SD 卡 Port 并清除缓存信息。
  * @param  hsdcard SD 卡设备句柄。
  * @retval SDCARD_OK 操作成功；重复反初始化同样成功。
  * @retval SDCARD_ERROR 句柄/Port 非法或底层反初始化失败。
  */
SDCard_StatusTypeDef SDCard_DeInit(SDCard_HandleTypeDef *hsdcard);

/**
  * @brief  根据当前介质检测状态处理一次插入或移除。
  * @param  hsdcard SD 卡设备句柄。
  * @note   调用者负责去抖；不得从中断服务函数调用。
  */
SDCard_StatusTypeDef SDCard_Refresh(SDCard_HandleTypeDef *hsdcard);

/**
  * @brief  读取一个或多个连续逻辑块。
  * @param  hsdcard SD 卡设备句柄。
  * @param  data 接收数据的缓冲区。
  * @param  start_block 起始逻辑块编号。
  * @param  block_count 连续读取的逻辑块数量。
  */
SDCard_StatusTypeDef SDCard_ReadBlocks(SDCard_HandleTypeDef *hsdcard,
                                       uint8_t *data,
                                       uint32_t start_block,
                                       uint32_t block_count);

/**
  * @brief  写入一个或多个连续逻辑块。
  * @param  hsdcard SD 卡设备句柄。
  * @param  data 待写入的数据缓冲区。
  * @param  start_block 起始逻辑块编号。
  * @param  block_count 连续写入的逻辑块数量。
  */
SDCard_StatusTypeDef SDCard_WriteBlocks(SDCard_HandleTypeDef *hsdcard,
                                        const uint8_t *data,
                                        uint32_t start_block,
                                        uint32_t block_count);

/** @brief 等待介质完成内部操作并回到可传输状态。 */
SDCard_StatusTypeDef SDCard_Sync(SDCard_HandleTypeDef *hsdcard);

/**
  * @brief  复制缓存的介质信息。
  * @param  hsdcard SD 卡设备句柄。
  * @param  info 接收信息快照的指针。
  */
SDCard_StatusTypeDef SDCard_GetInfo(const SDCard_HandleTypeDef *hsdcard,
                                    SDCard_InfoTypeDef *info);

/** @brief 获取设备当前持续状态；空句柄按 ERROR 处理。 */
SDCard_StateTypeDef SDCard_GetState(const SDCard_HandleTypeDef *hsdcard);

/**
  * @brief  读取当前 Port 的原始介质检测值。
  * @note   本函数不会自动改变句柄 State。
  */
bool SDCard_IsPresent(const SDCard_HandleTypeDef *hsdcard);

#ifdef __cplusplus
}
#endif

#endif /* SD_H */
