/**
 * @file flash_ftl_w25qxx_bridge.c
 * @brief 将分区相对 RawOps 转换为 W25Qxx 公共操作。
 */

#include "Adapters/bridge/flash_ftl_w25qxx/flash_ftl_w25qxx_bridge.h"

/**
 * @brief 将器件结果归一化为 RawOps 状态，并保留总线超时语义。
 * @param[in] bridge 已绑定的 Bridge；Device 在本次调用期间有效。
 * @param[in] status W25Qxx 当前调用的返回值。
 * @return OK/BUSY 映射为 RAW_OK/RAW_BUSY；失败按 LastBusStatus 区分 RAW_TIMEOUT 与 RAW_ERROR。
 * @note Start 的成功仅代表受理；Process 和 Quiesce 的成功语义分别由对应操作决定。
 */
static FlashFTL_RawStatusTypeDef bridge_status(FlashFTL_W25QxxBridgeTypeDef *bridge,
                                               W25Qxx_StatusTypeDef status)
{
    if (status == W25QXX_OK)
    {
        return FLASH_FTL_RAW_OK;
    }
    if (status == W25QXX_BUSY)
    {
        return FLASH_FTL_RAW_BUSY;
    }
    return bridge->Device->LastBusStatus == W25QXX_BUS_TIMEOUT ? FLASH_FTL_RAW_TIMEOUT
                                                               : FLASH_FTL_RAW_ERROR;
}

/**
 * @brief 以减法校验分区内完整范围，避免长度加法溢出。
 * @param[in] bridge 待检查的 Bridge，NULL 或未绑定时拒绝。
 * @param[in] address 分区相对字节偏移。
 * @param[in] length 非零请求字节数。
 * @return true 表示范围有效且基址加法安全；false 表示实例、长度或地址无效。
 * @note 分区基址与总长度已由 Bind 验证；此处不访问介质。
 */
static bool bridge_range(FlashFTL_W25QxxBridgeTypeDef *bridge, uint32_t address, uint32_t length)
{
    return bridge && bridge->Device && length && address < bridge->Size &&
           length <= bridge->Size - address && address <= UINT32_MAX - bridge->Base;
}

/**
 * @brief 返回绑定分区的容量及 W25Qxx 固定擦除、编程几何。
 * @param[in] context Bind 建立的长期 Bridge Context。
 * @param[out] geometry 接收分区字节数、4 KiB 擦除粒度和 256 B 页大小。
 * @return RAW_OK 表示已写入几何；Context 或输出为空时返回 RAW_ERROR。
 */
static FlashFTL_RawStatusTypeDef bridge_geometry(void *context,
                                                 FlashFTL_RawGeometryTypeDef *geometry)
{
    FlashFTL_W25QxxBridgeTypeDef *bridge = context;
    if (!bridge || !geometry)
    {
        return FLASH_FTL_RAW_ERROR;
    }
    *geometry = (FlashFTL_RawGeometryTypeDef){
        bridge->Size, W25QXX_SECTOR_ERASE_SIZE_BYTES, W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES};
    return FLASH_FTL_RAW_OK;
}

/**
 * @brief 校验分区范围并受理 W25Qxx 异步读取。
 * @param[in] context 已绑定的长期 Bridge Context。
 * @param[in] address 分区相对字节地址；实际读取对齐约束由 W25Qxx 校验。
 * @param[out] data 至少 length 字节的输出缓冲。
 * @param[in] length 非零读取字节数。
 * @return RAW_OK 表示受理；参数错误返回 RAW_ERROR，其余后端结果归一化为 RAW_BUSY/RAW_ERROR/RAW_TIMEOUT。
 * @note 成功仅表示受理；data 到 Process 完成或 Quiesce 成功前保持有效，DMA 条件由装配者保证。
 */
static FlashFTL_RawStatusTypeDef bridge_read(void *context,
                                             uint32_t address,
                                             uint8_t *data,
                                             uint32_t length)
{
    FlashFTL_W25QxxBridgeTypeDef *bridge = context;
    if (!data || !bridge_range(bridge, address, length))
    {
        return FLASH_FTL_RAW_ERROR;
    }
    return bridge_status(bridge,
                         W25Qxx_StartRead(bridge->Device, bridge->Base + address, data, length));
}

/**
 * @brief 校验页边界后受理分区内单页编程。
 * @param[in] context 已绑定的长期 Bridge Context。
 * @param[in] address 分区相对字节地址，不得使请求跨越 256 B 页。
 * @param[in] data 至少 length 字节的输入缓冲。
 * @param[in] length 1..256 字节，完整范围必须位于分区内。
 * @return RAW_OK 表示受理；参数错误返回 RAW_ERROR，其余后端结果归一化为 RAW_BUSY/RAW_ERROR/RAW_TIMEOUT。
 * @note 目标须已擦除；RAW_OK 不表示 NOR 内部编程完成，输入缓冲按 RawOps 生命周期保留。
 */
static FlashFTL_RawStatusTypeDef bridge_program(void *context,
                                                uint32_t address,
                                                const uint8_t *data,
                                                uint32_t length)
{
    FlashFTL_W25QxxBridgeTypeDef *bridge = context;
    if (!data || !bridge_range(bridge, address, length) ||
        length > W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES ||
        address % W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES + length > W25QXX_PAGE_PROGRAM_MAX_SIZE_BYTES)
    {
        return FLASH_FTL_RAW_ERROR;
    }
    return bridge_status(
        bridge, W25Qxx_ProgramPageStart(bridge->Device, bridge->Base + address, data, length));
}

/**
 * @brief 校验 4 KiB 对齐及完整范围后受理一个物理块擦除。
 * @param[in] context 已绑定的长期 Bridge Context。
 * @param[in] address 分区相对的 4 KiB 对齐地址。
 * @return RAW_OK 表示受理；参数错误返回 RAW_ERROR，其余后端结果归一化为 RAW_BUSY/RAW_ERROR/RAW_TIMEOUT。
 * @note 调用者负责确认目标可丢弃；本函数只启动擦除，FTL 在完成后验证整块擦除值。
 */
static FlashFTL_RawStatusTypeDef bridge_erase(void *context, uint32_t address)
{
    FlashFTL_W25QxxBridgeTypeDef *bridge = context;
    if (!bridge_range(bridge, address, W25QXX_SECTOR_ERASE_SIZE_BYTES) ||
        address % W25QXX_SECTOR_ERASE_SIZE_BYTES)
    {
        return FLASH_FTL_RAW_ERROR;
    }
    return bridge_status(bridge, W25Qxx_SectorEraseStart(bridge->Device, bridge->Base + address));
}

/**
 * @brief 查询 W25Qxx 异步操作进度并归一化结果。
 * @param[in] context 已绑定且有当前操作的长期 Bridge Context。
 * @return RAW_BUSY 表示待完成；RAW_OK 表示完成；RAW_ERROR/RAW_TIMEOUT 表示故障。
 * @note 仅由 FTL 普通执行上下文调用，不等待任务通知。
 */
static FlashFTL_RawStatusTypeDef bridge_process(void *context)
{
    FlashFTL_W25QxxBridgeTypeDef *bridge = context;
    return bridge_status(bridge, W25Qxx_Process(bridge->Device));
}

/**
 * @brief 转交原始后端安全收尾，确认控制器不再访问缓冲。
 * @param[in] context 已绑定的长期 Bridge Context。
 * @return RAW_OK 表示缓冲可安全归还；RAW_BUSY 须继续收尾；RAW_ERROR/RAW_TIMEOUT 不承诺缓冲安全。
 * @note 不取消 NOR 内部擦写，也不把器件恢复为 READY；恢复另行检查 WIP/QE。
 */
static FlashFTL_RawStatusTypeDef bridge_quiesce(void *context)
{
    FlashFTL_W25QxxBridgeTypeDef *bridge = context;
    return bridge_status(bridge, W25Qxx_Quiesce(bridge->Device));
}

static const FlashFTL_RawOpsTypeDef bridge_ops = {
    bridge_geometry, bridge_read, bridge_program, bridge_erase, bridge_process, bridge_quiesce};

/**
 * @brief 把 W25Qxx 公开原始操作绑定为 FTL RawOps，不擦写或拥有实例。
 * @param ftl 未在飞的 FTL 实例，由 Platform 长期持有。
 * @param bridge 长期有效的 Bridge Context，保存设备及分区相对寻址信息。
 * @param device 已识别且 READY 的 W25Q256 实例，后端须提供 Quiesce。
 * @param base 4 KiB 对齐的物理分区基址。
 * @param size 4 KiB 对齐的分区长度，完整范围不得越芯片。
 * @param memory FTL 长期表与工作区，其 DMA 条件由板级装配保证。
 * @retval FLASH_FTL_OK 已绑定，尚未打开逻辑卷。
 * @retval FLASH_FTL_INVALID_PARAM 器件、分区、操作表或内存条件无效。
 * @note 仅唯一普通上下文调用。Bridge 只转换两个 Component Interface，
 *       不等待任务、不管理 GC、不直接调用 HAL；分区检查覆盖完整请求长度。
 */
FlashFTL_StatusTypeDef FlashFTL_W25QxxBridge_Bind(FlashFTL_HandleTypeDef *ftl,
                                                  FlashFTL_W25QxxBridgeTypeDef *bridge,
                                                  W25Qxx_HandleTypeDef *device,
                                                  uint32_t base,
                                                  uint32_t size,
                                                  const FlashFTL_MemoryTypeDef *memory)
{
    W25Qxx_JedecIDTypeDef id;
    if (!ftl || !bridge || !device || !memory || !size || base % 4096 || size % 4096 ||
        W25Qxx_GetJedecID(device, &id) != W25QXX_OK || id.CapacityID != W25QXX_CAPACITY_ID_256MBIT)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    uint32_t bytes = 1UL << id.CapacityID;
    if (base >= bytes || size > bytes - base || !device->BusOps->Quiesce)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    *bridge = (FlashFTL_W25QxxBridgeTypeDef){device, base, size};
    return FlashFTL_Init(ftl, &bridge_ops, bridge, memory);
}
