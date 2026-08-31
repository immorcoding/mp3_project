/**
 * @file flash_ftl.h
 * @brief 可移植 NOR 逻辑组存储接口；所有内存和原始后端由调用者注入。
 */

#ifndef FLASH_FTL_H
#define FLASH_FTL_H

#include <stdbool.h>
#include <stdint.h>

#define FLASH_FTL_SECTOR_BYTES  512U  /**< FatFs 看到的逻辑扇区字节数。 */
#define FLASH_FTL_BLOCK_BYTES   4096U /**< 一个组版本独占的物理块字节数。 */
#define FLASH_FTL_PAGE_BYTES    256U  /**< 元数据与原始编程的页字节数。 */
#define FLASH_FTL_GROUP_SECTORS 7U    /**< 同一组版本共同提交的逻辑扇区数。 */

/**
 * @brief 逻辑请求的受理、推进和最终状态。
 * @note Start 返回 OK 只表示受理；Process 返回 OK 才表示完成。
 * RUNNING/WAIT 都保持请求所有权，不能开始另一请求或复用工作缓冲。
 */
typedef enum
{
    FLASH_FTL_OK = 0,        /**< 当前调用成功；是否已完成取决于调用的是 Start 还是 Process。 */
    FLASH_FTL_RUNNING,       /**< 软件阶段仍可继续推进。 */
    FLASH_FTL_WAIT,          /**< 等待原始操作或安全收尾，稍后重查。 */
    FLASH_FTL_INVALID_PARAM, /**< 参数、完整范围、几何或内存容量不满足要求。 */
    FLASH_FTL_NOT_READY,     /**< 实例已绑定，但卷尚未打开或已故障。 */
    FLASH_FTL_NO_SPACE,      /**< 无法安全分配且没有可回收块，已有数据仍可读取。 */
    FLASH_FTL_IO_ERROR,      /**< 原始访问失败或显式 Abort 后的最终错误。 */
    FLASH_FTL_CORRUPT,       /**< 提交关联、数据、版本选择或回读验证损坏。 */
    FLASH_FTL_INCOMPATIBLE,  /**< 格式/几何不兼容，或版本空间已耗尽。 */
    FLASH_FTL_INCOMPLETE,    /**< 最新格式代次未写入有效 READY。 */
    FLASH_FTL_UNFORMATTED,   /**< 不存在可识别的有效卷描述，不触发自动格式化。 */
    FLASH_FTL_BUSY           /**< 已有请求在飞，拒绝新请求而不修改原请求。 */
} FlashFTL_StatusTypeDef;

/** @brief 后端归一化状态，不直接泄漏 HAL/RTOS 类型。 */
typedef enum
{
    FLASH_FTL_RAW_OK = 0, /**< Start 时表示受理，Process 时表示完成，Quiesce 时表示缓冲安全。 */
    FLASH_FTL_RAW_BUSY,   /**< 原始操作或安全收尾尚未完成。 */
    FLASH_FTL_RAW_ERROR,  /**< 原始后端错误，不承诺硬件已停止。 */
    FLASH_FTL_RAW_TIMEOUT /**< 原始后端超时，仍须执行安全收尾。 */
} FlashFTL_RawStatusTypeDef;

/** @brief 绑定分区的原始几何，单位均为字节；首版只支持 4 KiB 擦除和 256 B 页。 */
typedef struct
{
    uint32_t SizeBytes;  /**< 分区总字节数，不是整颗芯片容量。 */
    uint32_t EraseBytes; /**< 物理擦除块字节数，必须为 4096。 */
    uint32_t PageBytes;  /**< 页编程字节数，必须为 256。 */
} FlashFTL_RawGeometryTypeDef;

/**
 * @brief FTL 拥有的原始 NOR 操作契约；实现者不得等待 RTOS。
 * @note 地址均为绑定分区内的相对字节偏移。Start 返回 RAW_OK 仅表示受理；
 *       数据和硬件完成通过 Process 返回 RAW_OK 确认，RAW_BUSY 表示稍后重查。
 *       每次只允许一笔在飞，参数缓冲到最终完成或 Quiesce 成功前保持有效。
 *       ProgramStart 仅编程一个页内范围，EraseStart 擦除一个对齐物理块。
 *       Quiesce 的 RAW_OK 只承诺控制器/DMA 不再访问缓冲，不承诺 NOR 内部操作取消。
 */
typedef struct
{
    FlashFTL_RawStatusTypeDef (*GetGeometry)(
        void *context,
        FlashFTL_RawGeometryTypeDef *geometry); /**< 同步查询分区几何，不启动阵列读写。 */
    FlashFTL_RawStatusTypeDef (*ReadStart)(
        void *context,
        uint32_t address,
        uint8_t *data,
        uint32_t length); /**< 受理分区相对读取，输出只能在 Process 成功后使用。 */
    FlashFTL_RawStatusTypeDef (*ProgramStart)(
        void *context,
        uint32_t address,
        const uint8_t *data,
        uint32_t length); /**< 受理页内编程，不允许跨页或覆盖未擦除内容。 */
    FlashFTL_RawStatusTypeDef (*EraseStart)(
        void *context, uint32_t address); /**< 受理一个对齐物理块擦除，不代替完成后的回读验证。 */
    FlashFTL_RawStatusTypeDef (*Process)(
        void *context); /**< 查询原始进度；成功返回前完成后端 Cache 收尾。 */
    FlashFTL_RawStatusTypeDef (*Quiesce)(
        void *context); /**< 确认控制器/DMA 不再访问缓冲，不取消 NOR 内部写擦。 */
} FlashFTL_RawOpsTypeDef;

/**
 * @brief 由装配者长期持有并注入的表和工作区；区域互不重叠。
 * @note 容量以数组元素数计，不是字节数。Component 只使用实际几何对应的元素；
 *       打开卷时重建表，不把 NOLOAD/SDRAM 残留当作有效映射。
 *       Work/Scratch 的 DMA 可达性、Cache line 对齐及独占条件由装配者保证。
 */
typedef struct
{
    uint32_t *Map;          /**< 至少 GroupCapacity 个物理块索引。 */
    uint64_t *Versions;     /**< 至少 GroupCapacity 个组版本号。 */
    uint8_t *BlockStates;   /**< 至少 BlockCapacity 个块状态。 */
    uint32_t GroupCapacity; /**< 不小于 GetInfo 返回的 GroupCount。 */
    uint32_t BlockCapacity; /**< 不小于 GetInfo 返回的 DataBlockCount。 */
    uint8_t *Work;          /**< 至少 4096 B，保存一份完整组记录。 */
    uint8_t *Scratch;       /**< 至少 512 B，用于验证且不得覆盖 Work。 */
} FlashFTL_MemoryTypeDef;

/** @brief 只读逻辑几何；预留预算是全池容量预算，不是固定地址区域。 */
typedef struct
{
    uint32_t SectorCount;    /**< 对外 512 B 逻辑扇区数。 */
    uint32_t GroupCount;     /**< 可映射逻辑组数，每组包含七个扇区。 */
    uint32_t DataBlockCount; /**< 扣除两个卷描述块后的物理数据块数。 */
    uint32_t ReserveBlocks;  /**< 数据物理块中不计入逻辑容量的预算，所有块仍参与循环分配。 */
} FlashFTL_InfoTypeDef;

/** @brief 只读诊断：格式/代次、当前块快照和本次绑定的擦除累计。 */
typedef struct
{
    uint32_t FormatVersion;        /**< 本实现的持久化格式协议版本。 */
    uint64_t Epoch;                /**< 当前处理或打开的格式代次，不是组版本。 */
    uint32_t ValidGroups;          /**< 当前有效块数；未就绪时不遍历表，返回零。 */
    uint32_t FreeBlocks;           /**< 已确认整块擦除的空闲块计数，扫描中可能尚未统计完。 */
    uint32_t StaleBlocks;          /**< 当前失效块数；未就绪时返回零。 */
    uint32_t SessionErases;        /**< 本次 Init 以来的数据块累计擦除数，不含卷头且不持久化。 */
    FlashFTL_StatusTypeDef Result; /**< 最近请求的推进或最终结果。 */
    FlashFTL_RawStatusTypeDef
        LastRawStatus; /**< 最近原始访问的归一化结果；不代表 NOR 当前 WIP 快照。 */
} FlashFTL_DiagnosticsTypeDef;

/**
 * @brief 仅 RAM 使用的请求类别，与持久化格式无关。
 * @note 为静态句柄布局提供类型定义；调用者不得自行修改请求类别。
 */
typedef enum
{
    FLASH_FTL_OP_OPEN = 1,
    FLASH_FTL_OP_FORMAT,
    FLASH_FTL_OP_READ,
    FLASH_FTL_OP_WRITE,
    FLASH_FTL_OP_GC,
    FLASH_FTL_OP_SYNC
} FlashFTL_OperationTypeDef;

/**
 * @brief FTL 的有限推进阶段；这些枚举不写入 Flash。
 * @details 卷描述选择 -> 格式化或扫描 -> 逻辑访问/提交 -> GC；
 * 原始请求受理后先置 Pending，只有 Process 确认完成才转入 NextStep。
 * 任意 I/O 错误转到 FAULT，待 Quiesce 成功后才能发布最终失败。
 * @note 为静态句柄布局提供类型定义；调用者不得自行推进内部阶段。
 */
typedef enum
{
    FLASH_FTL_STEP_VOLUME_A = 1,
    FLASH_FTL_STEP_VOLUME_A_DONE,
    FLASH_FTL_STEP_VOLUME_B,
    FLASH_FTL_STEP_VOLUME_B_DONE,
    FLASH_FTL_STEP_SELECT_VOLUME,
    FLASH_FTL_STEP_FORMAT_ERASE_VOLUME,
    FLASH_FTL_STEP_FORMAT_VERIFY_VOLUME,
    FLASH_FTL_STEP_FORMAT_CHECK_VOLUME,
    FLASH_FTL_STEP_FORMAT_PREPARE,
    FLASH_FTL_STEP_FORMAT_VERIFY_PREPARE,
    FLASH_FTL_STEP_FORMAT_CHECK_PREPARE,
    FLASH_FTL_STEP_FORMAT_ERASE_DATA,
    FLASH_FTL_STEP_FORMAT_VERIFY_DATA,
    FLASH_FTL_STEP_FORMAT_CHECK_DATA,
    FLASH_FTL_STEP_FORMAT_READY,
    FLASH_FTL_STEP_FORMAT_VERIFY_READY,
    FLASH_FTL_STEP_FORMAT_CHECK_READY,
    FLASH_FTL_STEP_SCAN,
    FLASH_FTL_STEP_SCAN_FOOTER,
    FLASH_FTL_STEP_SCAN_DONE,
    FLASH_FTL_STEP_SCAN_CHECK_EMPTY,
    FLASH_FTL_STEP_VALIDATE,
    FLASH_FTL_STEP_VALIDATE_DONE,
    FLASH_FTL_STEP_READ,
    FLASH_FTL_STEP_READ_DONE,
    FLASH_FTL_STEP_WRITE,
    FLASH_FTL_STEP_WRITE_OLD_DONE,
    FLASH_FTL_STEP_WRITE_ALLOCATE,
    FLASH_FTL_STEP_WRITE_PAGE,
    FLASH_FTL_STEP_WRITE_VERIFY,
    FLASH_FTL_STEP_WRITE_VERIFY_DONE,
    FLASH_FTL_STEP_WRITE_COMMIT,
    FLASH_FTL_STEP_WRITE_COMMIT_VERIFY,
    FLASH_FTL_STEP_WRITE_COMMIT_DONE,
    FLASH_FTL_STEP_SYNC,
    FLASH_FTL_STEP_GC,
    FLASH_FTL_STEP_GC_VERIFY,
    FLASH_FTL_STEP_GC_DONE,
    FLASH_FTL_STEP_FAULT
} FlashFTL_StepTypeDef;

/** @brief 静态分配所需的实例存储；字段只供本 Module 使用，调用者不得自行改写。 */
typedef struct
{
    const FlashFTL_RawOpsTypeDef *Ops;       /**< 借用的长期 RawOps，不拥有操作表。 */
    void *Context;                           /**< 原样传给 RawOps 的长期后端上下文。 */
    FlashFTL_MemoryTypeDef Memory;           /**< 注入内存的描述副本，不拥有或分配这些区域。 */
    FlashFTL_InfoTypeDef Info;               /**< 根据几何与预留比例计算的逻辑容量。 */
    FlashFTL_DiagnosticsTypeDef Diagnostics; /**< 操作结果与会话累计，查询时补充当前块快照。 */
    bool Ready;                 /**< 卷已恢复；仍须结合 Active 判定是否能受理新请求。 */
    bool Active;                /**< 请求仍占有实例，直到正常完成或安全收尾才清零。 */
    bool Pending;               /**< 当前有一笔原始操作等待 Process 确认。 */
    FlashFTL_StepTypeDef Step;              /**< 当前软件阶段，不持久化。 */
    FlashFTL_StepTypeDef NextStep;          /**< 原始操作成功后的续行阶段。 */
    FlashFTL_OperationTypeDef Operation;    /**< 当前请求类别。 */
    uint32_t Index;             /**< 扫描、格式化或 GC 使用的数据物理块索引。 */
    uint32_t Group;             /**< 当前逻辑组索引。 */
    uint32_t Target;            /**< 阶段相关目标：卷描述槽或新数据物理块索引。 */
    uint32_t OldBlock;          /**< 当前写请求替换的旧数据块，可能为 UNMAPPED。 */
    uint32_t Page;              /**< 当前阶段的页、校验片段或副本处理进度。 */
    uint32_t AllocationCursor;  /**< 循环分配游标，重启扫描后重新建立。 */
    uint32_t GcCursor;          /**< 循环回收游标，不持久化。 */
    uint32_t FreeBlocks;        /**< 已验证擦净的物理块数量，不包含失效但未擦除的块。 */
    uint32_t Lba;               /**< 当前读写请求的起始逻辑扇区。 */
    uint32_t Count;             /**< 当前请求的总扇区数。 */
    uint32_t Done;              /**< 已复制读取或已提交写入的扇区数。 */
    uint32_t Chunk;             /**< 本轮组内处理的扇区数，不跨逻辑组。 */
    uint8_t *ReadBuffer;        /**< 借用的请求输出，完成或安全收尾前保持有效。 */
    const uint8_t *WriteBuffer; /**< 借用的请求输入，完成或安全收尾前不得改写。 */
    /* 格式代次与组版本是两个独立维度，不可互相替代。 */
    uint64_t Epoch;           /**< 本卷格式代次，隔离历次格式化的记录。 */
    uint64_t NewVersion;      /**< 当前写入组的新版本号。 */
    uint64_t VolumeEpoch[2];  /**< 两个卷描述槽解析出的有效代次，零表示未识别。 */
    uint32_t VolumePhase[2];  /**< 两个卷槽各自的 PREPARING/READY 阶段。 */
    bool VolumeCompatible[2]; /**< 各槽格式版本和几何是否兼容。 */
    uint32_t FirstVolume;     /**< 本次格式化先处理的卷槽，避免先破坏唯一最新描述。 */
    bool GcRequested;         /**< 后台 GC 的低/高水位滞回状态。 */
    bool GcForWrite;          /**< 本次 GC 是否为前台分配让出空闲块，决定完成后续行。 */
} FlashFTL_HandleTypeDef;

FlashFTL_StatusTypeDef FlashFTL_Init(FlashFTL_HandleTypeDef *hftl,
                                     const FlashFTL_RawOpsTypeDef *ops,
                                     void *context,
                                     const FlashFTL_MemoryTypeDef *memory);
FlashFTL_StatusTypeDef FlashFTL_OpenStart(FlashFTL_HandleTypeDef *hftl);
FlashFTL_StatusTypeDef FlashFTL_FormatStart(FlashFTL_HandleTypeDef *hftl);
FlashFTL_StatusTypeDef FlashFTL_ReadStart(FlashFTL_HandleTypeDef *hftl,
                                          uint32_t lba,
                                          uint8_t *data,
                                          uint32_t count);
FlashFTL_StatusTypeDef FlashFTL_WriteStart(FlashFTL_HandleTypeDef *hftl,
                                           uint32_t lba,
                                           const uint8_t *data,
                                           uint32_t count);
FlashFTL_StatusTypeDef FlashFTL_SyncStart(FlashFTL_HandleTypeDef *hftl);
FlashFTL_StatusTypeDef FlashFTL_Abort(FlashFTL_HandleTypeDef *hftl);
FlashFTL_StatusTypeDef FlashFTL_MaintainStart(FlashFTL_HandleTypeDef *hftl);
bool FlashFTL_IsReady(const FlashFTL_HandleTypeDef *hftl);
FlashFTL_StatusTypeDef FlashFTL_GetDiagnostics(const FlashFTL_HandleTypeDef *hftl,
                                               FlashFTL_DiagnosticsTypeDef *diagnostics);
FlashFTL_StatusTypeDef FlashFTL_GetInfo(const FlashFTL_HandleTypeDef *hftl,
                                        FlashFTL_InfoTypeDef *info);
FlashFTL_StatusTypeDef FlashFTL_Process(FlashFTL_HandleTypeDef *hftl);

#endif /* FLASH_FTL_H */
