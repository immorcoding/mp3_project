/**
 * @file flash_ftl.c
 * @brief NOR 逻辑组存储：异地提交、扫描恢复与失效块回收。
 * @details
 * 每个 4 KiB 数据块保存一个七扇区组版本：256 B 头、3584 B 数据和 256 B 提交页。
 * 仅完成新记录与提交回读后切换 RAM 映射；旧记录由 GC 后续擦除。
 * 上电先选择提交版本，再完整校验获选记录。所有介质访问经注入的 RawOps，
 * 本 Module 不等待 RTOS、不分配内存、不知道芯片物理分区基址。
 * @note 仅唯一普通上下文使用。RUNNING/WAIT 均保持请求与缓冲所有权；
 * 故障必须经 Quiesce 安全收尾。不承诺跨组/FatFs 事务原子，也不自动格式化。
 */

#include "Components/flash_ftl/flash_ftl.h"
#include "Components/flash_ftl/flash_ftl_config.h"
#include <string.h>

/**
 * @brief RAM 中的物理块分类；FREE 必须经过全块擦除值验证，不能由空头推断。
 * @note BlockStates 表仍以 uint8_t 紧凑存储，不按枚举类型分配数组。
 */
typedef enum
{
    FLASH_FTL_BLOCK_UNKNOWN, /**< 扫描前未知，不能当作空闲。 */
    FLASH_FTL_BLOCK_FREE,    /**< 已确认整块为擦除值。 */
    FLASH_FTL_BLOCK_STALE,   /**< 旧版本或已替换，待 GC 擦除。 */
    FLASH_FTL_BLOCK_VALID    /**< 当前有效组版本所在块。 */
} FlashFTL_BlockStateTypeDef;

/**
 * @brief 验证给定范围是否全部为 NOR 擦除值 0xFF。
 * @param[in] data 已读回的有效字节范围。
 * @param[in] length 待检查字节数。
 * @retval true 全部为 0xFF。
 * @retval false 至少存在一个非擦除值字节。
 * @note 判定空闲块时必须传入整块；仅检查头/提交页不能排除中部残留数据。
 */
static bool flash_ftl_is_erased(const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
    {
        if (data[i] != 255U)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief 从持久化字节序列解码小端 32 位字段。
 * @param[in] data 至少可读 4 B 的字段首地址。
 * @return 解码后的整数。
 * @note 逐字节读取，避免结构体布局、主机字节序和非对齐访问影响盘上格式。
 */
static uint32_t flash_ftl_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

/**
 * @brief 按固定小端格式编码 32 位字段，不访问 Flash。
 * @param[out] data 至少可写 4 B 的字段位置。
 * @param[in] value 待编码值。
 * @note 不把 C 结构体直接落盘，保证主机测试与 MCU 使用相同格式。
 */
static void flash_ftl_write_u32(uint8_t *data, uint32_t value)
{
    for (uint32_t i = 0; i < 4; i++)
    {
        data[i] = (uint8_t)(value >> (i * 8));
    }
}

/**
 * @brief 从持久化字节序列解码 64 位代次或组版本。
 * @param[in] data 至少可读 8 B 的小端字段。
 * @return 解码后的整数。
 * @note 由两个 32 位字段组合，避免对非对齐版本字段直接解引用。
 */
static uint64_t flash_ftl_read_u64(const uint8_t *data)
{
    return flash_ftl_read_u32(data) | ((uint64_t)flash_ftl_read_u32(data + 4) << 32);
}

/**
 * @brief 按小端格式编码 64 位代次或组版本。
 * @param[out] data 至少可写 8 B 的字段位置。
 * @param[in] value 待编码值。
 * @note 此处只更新内存；原子可见性由最后写入的提交页决定。
 */
static void flash_ftl_write_u64(uint8_t *data, uint64_t value)
{
    flash_ftl_write_u32(data, (uint32_t)value);
    flash_ftl_write_u32(data + 4, (uint32_t)(value >> 32));
}

/**
 * @brief 计算格式版本 1 使用的反射 CRC32。
 * @param[in] data 参与校验的字节范围。
 * @param[in] length 校验长度，单位字节。
 * @return 初值 0xFFFFFFFF、反射多项式 0xEDB88320、最终取反的 CRC32。
 * @note 元数据调用者覆盖页前 252 B，数据调用者覆盖完整 512 B 扇区。
 *       不依赖 MCU CRC 外设；CRC 仅检测错误，不保证任意掉电或碰撞可恢复。
 */
static uint32_t flash_ftl_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (uint32_t b = 0; b < 8; b++)
        {
            crc = (crc >> 1) ^ ((0U - (crc & 1U)) & FLASH_FTL_CRC_POLYNOMIAL);
        }
    }
    return ~crc;
}

/**
 * @brief 将元数据页的 CRC 写入尾部字段。
 * @param[in,out] data 完整 256 B 元数据页，前 252 B 必须已编码完毕。
 * @note CRC 不包含自身；这里只封装内存记录，不代表提交页已持久化。
 */
static void flash_ftl_seal_page(uint8_t *data)
{
    flash_ftl_write_u32(data + FLASH_FTL_CRC_OFFSET, flash_ftl_crc32(data, FLASH_FTL_CRC_OFFSET));
}

/**
 * @brief 检查元数据页自身 CRC，不判断其业务语义。
 * @param[in] data 至少 256 B 的完整元数据页。
 * @return true 表示前 252 B 与末尾 CRC 一致，否则为 false。
 * @note 魔数、格式版本、epoch 和记录关联仍须由上层校验，不能只凭 CRC 接受记录。
 */
static bool flash_ftl_page_crc_is_valid(const uint8_t *data)
{
    return flash_ftl_read_u32(data + FLASH_FTL_CRC_OFFSET) ==
           flash_ftl_crc32(data, FLASH_FTL_CRC_OFFSET);
}

/**
 * @brief 将数据物理块索引转换为分区内相对字节偏移。
 * @param[in] data_block 已由调用者限定在 DataBlockCount 内的数据块索引。
 * @return 跳过前两个卷描述块后的 4 KiB 对齐偏移。
 * @note 不叠加芯片分区基址，该转换属于 Bridge；本函数不做范围检查。
 */
static uint32_t flash_ftl_data_block_address(uint32_t data_block)
{
    return (data_block + 2U) * FLASH_FTL_BLOCK_BYTES;
}

/**
 * @brief 结束已完成或已安全收尾的请求，发布最终结果。
 * @param[in,out] hftl 当前独占实例。
 * @param[in] status 要发布的最终状态，不应是 RUNNING 或 WAIT。
 * @return 原样返回 status。
 * @pre 所有原始操作已结束，或 Quiesce 已确认硬件不再访问缓冲。
 * @note NO_SPACE 保留卷就绪以便读出已有数据；其他失败撤销 Ready。
 *       本函数本身不停止 DMA，不能从在飞错误路径直接调用。
 */
static FlashFTL_StatusTypeDef flash_ftl_complete(FlashFTL_HandleTypeDef *hftl,
                                                 FlashFTL_StatusTypeDef status)
{
    hftl->Active = false;
    hftl->Diagnostics.Result = status;
    if (status != FLASH_FTL_OK && status != FLASH_FTL_NO_SPACE)
    {
        hftl->Ready = false;
    }
    return status;
}

/**
 * @brief 锁存失败并转入安全收尾，暂不归还请求所有权。
 * @param[in,out] hftl 正在处理请求的独占实例。
 * @param[in] status 待收尾后发布的失败结果。
 * @retval FLASH_FTL_RUNNING 请求仍在飞，须继续 Process。
 * @note 清除 Pending 只是停止正常状态推进，不表示硬件已取消。
 *       Active 保持到 Quiesce 成功；已提交组不会被此过程回滚。
 */
static FlashFTL_StatusTypeDef flash_ftl_begin_fault_cleanup(FlashFTL_HandleTypeDef *hftl,
                                                            FlashFTL_StatusTypeDef status)
{
    hftl->Ready = false;
    hftl->Pending = false;
    hftl->Diagnostics.Result = status;
    hftl->Step = FLASH_FTL_STEP_FAULT;
    return FLASH_FTL_RUNNING;
}

/**
 * @brief 接收 RawOps 启动结果，并保存成功完成后的续行阶段。
 * @param[in,out] hftl 当前请求实例。
 * @param[in] status 原始 Start 的立即返回值。
 * @param[in] next 原始 Process 成功后才能进入的阶段。
 * @retval FLASH_FTL_RUNNING 保持请求在飞，等待原始操作或故障收尾。
 * @note RAW_OK 仅表示受理，不能立即消费读缓冲。此独占模型不应出现 RAW_BUSY；
 *       启动的任何非 OK 结果均转入安全收尾，防止部分启动后过早复用缓冲。
 */
static FlashFTL_StatusTypeDef flash_ftl_accept_raw_start(FlashFTL_HandleTypeDef *hftl,
                                                         FlashFTL_RawStatusTypeDef status,
                                                         FlashFTL_StepTypeDef next)
{
    hftl->Diagnostics.LastRawStatus = status;
    if (status != FLASH_FTL_RAW_OK)
    {
        return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_IO_ERROR);
    }
    hftl->Pending = true;
    hftl->NextStep = next;
    return FLASH_FTL_RUNNING;
}

/**
 * @brief 提交分区相对读取，并将续行绑定到原始完成事件。
 * @param[in,out] hftl 当前请求实例。
 * @param[in] address 分区内字节偏移。
 * @param[out] data 原始接收缓冲，完成或安全收尾前保持有效。
 * @param[in] length 非零字节数，完整范围由已验证几何与调用阶段保证。
 * @param[in] next 原始读取完成后的续行阶段。
 * @retval FLASH_FTL_RUNNING 已进入等待或故障收尾。
 * @note 本层不等待任务；读取结果只能在原始 Process 成功后使用。
 */
static FlashFTL_StatusTypeDef flash_ftl_start_raw_read(FlashFTL_HandleTypeDef *hftl,
                                                       uint32_t address,
                                                       uint8_t *data,
                                                       uint32_t length,
                                                       FlashFTL_StepTypeDef next)
{
    return flash_ftl_accept_raw_start(
        hftl, hftl->Ops->ReadStart(hftl->Context, address, data, length), next);
}

/**
 * @brief 提交一个完整 256 B 页的编程，不等待 NOR 写入完成。
 * @param[in,out] hftl 当前请求实例。
 * @param[in] address 分区内页对齐偏移。
 * @param[in] data 完整页内容，原始操作结束前不得改写。
 * @param[in] next 编程完成后的续行阶段。
 * @retval FLASH_FTL_RUNNING 已进入等待或故障收尾。
 * @pre 目标页已确认擦除，当前擦除周期内没有编程过。
 * @note 头、数据和提交均按页写；跳过全 FF 页由调用阶段决定。
 */
static FlashFTL_StatusTypeDef flash_ftl_start_raw_program(FlashFTL_HandleTypeDef *hftl,
                                                          uint32_t address,
                                                          const uint8_t *data,
                                                          FlashFTL_StepTypeDef next)
{
    return flash_ftl_accept_raw_start(
        hftl, hftl->Ops->ProgramStart(hftl->Context, address, data, 256), next);
}

/**
 * @brief 提交一个 4 KiB 块擦除，将回读验证留给续行阶段。
 * @param[in,out] hftl 当前请求实例。
 * @param[in] address 分区内块对齐偏移。
 * @param[in] next 原始擦除完成后的阶段。
 * @retval FLASH_FTL_RUNNING 已进入等待或故障收尾。
 * @pre 调用者已确认目标不是需要保留的当前有效块，或已进入显式格式化流程。
 * @note 控制器报告擦除完成后仍须回读，不能直接把目标登记为空闲。
 */
static FlashFTL_StatusTypeDef flash_ftl_start_raw_erase(FlashFTL_HandleTypeDef *hftl,
                                                        uint32_t address,
                                                        FlashFTL_StepTypeDef next)
{
    return flash_ftl_accept_raw_start(hftl, hftl->Ops->EraseStart(hftl->Context, address), next);
}

/**
 * @brief 清理非持久化映射、块状态与游标，为扫描或新格式化准备。
 * @param[in,out] hftl 已绑定且表容量通过校验的实例。
 * @note NOLOAD/SDRAM 残留不可信；所有块先置 UNKNOWN，扫描或擦除验证后再分类。
 *       不清空 Work，避免覆盖当前阶段仍在使用的记录；会话擦除计数不在此重置。
 */
static void flash_ftl_clear_tables(FlashFTL_HandleTypeDef *hftl)
{
    for (uint32_t i = 0; i < hftl->Info.GroupCount; i++)
    {
        hftl->Memory.Map[i] = FLASH_FTL_UNMAPPED;
        hftl->Memory.Versions[i] = 0;
    }
    memset(hftl->Memory.BlockStates, FLASH_FTL_BLOCK_UNKNOWN, hftl->Info.DataBlockCount);
    hftl->FreeBlocks = 0;
    hftl->AllocationCursor = 0;
    hftl->GcCursor = 0;
    hftl->GcRequested = false;
}

/**
 * @brief 在 Work 页中编码指定阶段的卷描述。
 * @param[in,out] hftl 提供当前 epoch、几何和至少 256 B 的 Work。
 * @param[in] phase PREPARING 或 READY，与目标物理页位置对应。
 * @note 未使用字节填 FF，CRC 覆盖前 252 B。此函数只生成记录；
 *       两份 PREPARING 都已持久化并验证后，状态机才允许擦除数据块。
 */
static void flash_ftl_encode_volume(FlashFTL_HandleTypeDef *hftl, uint32_t phase)
{
    uint8_t *data = hftl->Memory.Work;
    memset(data, 255, 256);
    flash_ftl_write_u32(data, FLASH_FTL_VOLUME_MAGIC);
    flash_ftl_write_u32(data + 4, FLASH_FTL_FORMAT_VERSION);
    flash_ftl_write_u64(data + 8, hftl->Epoch);
    flash_ftl_write_u32(data + 16, phase);
    flash_ftl_write_u32(data + 20, hftl->Info.DataBlockCount);
    flash_ftl_write_u32(data + 24, hftl->Info.GroupCount);
    flash_ftl_write_u32(data + 28, FLASH_FTL_SECTOR_BYTES);
    flash_ftl_write_u32(data + 32, FLASH_FTL_BLOCK_BYTES);
    flash_ftl_write_u32(data + 36, FLASH_FTL_GROUP_SECTORS);
    flash_ftl_seal_page(data);
}

/**
 * @brief 从 Work 中解析一份卷描述块的 PREPARING/READY 两页。
 * @param[in,out] hftl Work 前 512 B 已完成原始读取的实例。
 * @param[in] slot 输出卷槽索引，只能为 0 或 1。
 * @note 选择该槽有效记录中的最高 epoch；同 epoch 的 READY 页优先。
 *       兼容性单独记录，不把新版 PREPARING 误当成旧版 READY；
 *       没有有效页时留下零 epoch，不自动修复或格式化。
 */
static void flash_ftl_parse_volume(FlashFTL_HandleTypeDef *hftl, uint32_t slot)
{
    hftl->VolumeEpoch[slot] = 0;
    hftl->VolumePhase[slot] = 0;
    hftl->VolumeCompatible[slot] = false;
    for (uint32_t i = 0; i < 2; i++)
    {
        const uint8_t *data = hftl->Memory.Work + i * 256;
        if (flash_ftl_read_u32(data) != FLASH_FTL_VOLUME_MAGIC ||
            !flash_ftl_page_crc_is_valid(data) || !flash_ftl_read_u64(data + 8) ||
            flash_ftl_read_u32(data + 16) != (i ? FLASH_FTL_READY : FLASH_FTL_PREPARING))
        {
            continue;
        }
        if (flash_ftl_read_u64(data + 8) >= hftl->VolumeEpoch[slot])
        {
            hftl->VolumeEpoch[slot] = flash_ftl_read_u64(data + 8);
            hftl->VolumePhase[slot] = flash_ftl_read_u32(data + 16);
            hftl->VolumeCompatible[slot] =
                flash_ftl_read_u32(data + 4) == FLASH_FTL_FORMAT_VERSION &&
                flash_ftl_read_u32(data + 20) == hftl->Info.DataBlockCount &&
                flash_ftl_read_u32(data + 24) == hftl->Info.GroupCount &&
                flash_ftl_read_u32(data + 28) == 512 && flash_ftl_read_u32(data + 32) == 4096 &&
                flash_ftl_read_u32(data + 36) == 7;
        }
    }
}

/**
 * @brief 检查组头自身的格式、CRC 以及非零代次/版本。
 * @param[in] record 至少可读 256 B 组头的记录首地址。
 * @return true 表示组头自身合法，否则为 false。
 * @note 尚未核验提交页、所属逻辑组范围或 payload，不能据此让记录可见。
 */
static bool flash_ftl_header_is_valid(const uint8_t *record)
{
    return flash_ftl_read_u32(record) == FLASH_FTL_HEADER_MAGIC &&
           flash_ftl_page_crc_is_valid(record) &&
           flash_ftl_read_u32(record + 4) == FLASH_FTL_FORMAT_VERSION &&
           flash_ftl_read_u64(record + 8) && flash_ftl_read_u64(record + 20);
}

/**
 * @brief 独立检查尾部提交页的魔数、格式版本与自身 CRC。
 * @param[in] record 完整 4 KiB 记录缓冲，尾部提交页已读入。
 * @return true 表示提交页自身合法，否则为 false。
 * @note 故意不依赖组头：旧失效块可能被 GC 擦坏头部而保留提交页。
 *       启动先据此选择最新版本，再完整验证获选记录，避免误判旧块撕裂。
 */
static bool flash_ftl_commit_is_valid(const uint8_t *record)
{
    const uint8_t *data = record + FLASH_FTL_COMMIT_OFFSET;
    return flash_ftl_read_u32(data) == FLASH_FTL_COMMIT_MAGIC &&
           flash_ftl_page_crc_is_valid(data) &&
           flash_ftl_read_u32(data + 4) == FLASH_FTL_FORMAT_VERSION;
}

/**
 * @brief 核验组头与提交页是否指向同一个已提交组版本。
 * @param[in] record 头页和尾页均已读入的 4 KiB 记录缓冲。
 * @return true 表示两页合法且 epoch、组号、版本和头 CRC 绑定一致。
 * @note 不校验 payload；调用者仍需确认它属于当前卷/请求，并检查数据 CRC。
 */
static bool flash_ftl_record_is_committed(const uint8_t *record)
{
    const uint8_t *data = record + FLASH_FTL_COMMIT_OFFSET;
    return flash_ftl_header_is_valid(record) && flash_ftl_commit_is_valid(record) &&
           flash_ftl_read_u64(data + 8) == flash_ftl_read_u64(record + 8) &&
           flash_ftl_read_u32(data + 16) == flash_ftl_read_u32(record + 16) &&
           flash_ftl_read_u64(data + 20) == flash_ftl_read_u64(record + 20) &&
           flash_ftl_read_u32(data + 28) == flash_ftl_read_u32(record + FLASH_FTL_CRC_OFFSET);
}

/**
 * @brief 校验完整提交关联以及七个逻辑扇区的数据 CRC。
 * @param[in] record 已完整读入的 4 KiB 组记录。
 * @return true 表示关联与所有数据 CRC 一致，否则为 false。
 * @note 启动第二遍、普通读取和局部写前读旧组共用此检查；
 *       当前 epoch、目标组号及预期版本仍由调用阶段确认，不静默回退旧数据。
 */
static bool flash_ftl_record_data_is_valid(const uint8_t *record)
{
    if (!flash_ftl_record_is_committed(record))
    {
        return false;
    }
    for (uint32_t i = 0; i < 7; i++)
    {
        if (flash_ftl_read_u32(record + 28 + i * 4) != flash_ftl_crc32(record + 256 + i * 512, 512))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief 为 Work 中已合并的新组数据生成头页和提交页。
 * @param[in,out] hftl 提供 Epoch、Group、NewVersion 及完整 Work。
 * @pre Work 的 256..3839 字节已包含全部七个扇区的最终内容。
 * @note 保留 payload，只编码两端元数据；提交页绑定头页 CRC。
 *       状态机先写头和数据、读回验证，再最后写提交页，不能把内存编码当成落盘。
 */
static void flash_ftl_encode_record(FlashFTL_HandleTypeDef *hftl)
{
    uint8_t *record = hftl->Memory.Work, *data = record + FLASH_FTL_COMMIT_OFFSET;
    memset(record, 255, 256);
    flash_ftl_write_u32(record, FLASH_FTL_HEADER_MAGIC);
    flash_ftl_write_u32(record + 4, FLASH_FTL_FORMAT_VERSION);
    flash_ftl_write_u64(record + 8, hftl->Epoch);
    flash_ftl_write_u32(record + 16, hftl->Group);
    flash_ftl_write_u64(record + 20, hftl->NewVersion);
    for (uint32_t i = 0; i < 7; i++)
    {
        flash_ftl_write_u32(record + 28 + i * 4, flash_ftl_crc32(record + 256 + i * 512, 512));
    }
    flash_ftl_seal_page(record);
    memset(data, 255, 256);
    flash_ftl_write_u32(data, FLASH_FTL_COMMIT_MAGIC);
    flash_ftl_write_u32(data + 4, FLASH_FTL_FORMAT_VERSION);
    flash_ftl_write_u64(data + 8, hftl->Epoch);
    flash_ftl_write_u32(data + 16, hftl->Group);
    flash_ftl_write_u64(data + 20, hftl->NewVersion);
    flash_ftl_write_u32(data + 28, flash_ftl_read_u32(record + FLASH_FTL_CRC_OFFSET));
    flash_ftl_seal_page(data);
}

/**
 * @brief 将当前请求未完成部分切分到一个逻辑组内。
 * @param[in,out] hftl Lba、Count、Done 有效且 Done 小于 Count 的实例。
 * @note 只更新 Group 与 Chunk，Chunk 以扇区计且不跨七扇区组。
 *       Done 由对应读复制或写提交完成阶段增加，因此跨组失败可能已有部分完成。
 */
static void flash_ftl_select_request_group(FlashFTL_HandleTypeDef *hftl)
{
    uint32_t lba = hftl->Lba + hftl->Done;
    hftl->Group = lba / 7;
    hftl->Chunk = 7 - lba % 7;
    if (hftl->Chunk > hftl->Count - hftl->Done)
    {
        hftl->Chunk = hftl->Count - hftl->Done;
    }
}

/**
 * @brief 在单执行者模型中受理请求并占有实例。
 * @param[in,out] hftl 已绑定实例。
 * @param[in] op 请求类别，不是盘上格式字段。
 * @param[in] step 请求的首个软件阶段。
 * @retval FLASH_FTL_OK 已受理，尚未发起原始访问。
 * @retval FLASH_FTL_BUSY 原请求仍在飞，不修改它。
 * @retval FLASH_FTL_INVALID_PARAM 实例无效或未绑定。
 * @note 这里只检查通用前置条件；各公开入口负责容量、Ready 和缓冲检查。
 *       没有锁或并发仲裁能力，不能用 Active 判断替代调用者的单任务约束。
 */
static FlashFTL_StatusTypeDef flash_ftl_start_operation(FlashFTL_HandleTypeDef *hftl,
                                                        FlashFTL_OperationTypeDef op,
                                                        FlashFTL_StepTypeDef step)
{
    if (!hftl || !hftl->Ops)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    if (hftl->Active)
    {
        return FLASH_FTL_BUSY;
    }
    hftl->Active = true;
    hftl->Pending = false;
    hftl->Operation = op;
    hftl->Step = step;
    hftl->Diagnostics.Result = FLASH_FTL_RUNNING;
    return FLASH_FTL_OK;
}

/**
 * @brief 绑定原始后端与长期内存，校验几何及表容量，不擦写介质。
 * @param hftl 首次使用或硬件已安全静止的句柄，不得重绑在飞实例。
 * @param ops 长期有效的原始操作表，必须提供全部操作及 Quiesce。
 * @param context 后端长期上下文；是否允许 NULL 由后端约定。
 * @param memory 内存描述会复制到句柄，其指向的所有区域必须长期有效且互不重叠。
 * @retval FLASH_FTL_OK 已绑定，仍需 OpenStart 或显式 FormatStart 后推进 Process。
 * @retval FLASH_FTL_INVALID_PARAM 指针、操作表、几何、配置或内存容量无效。
 * @note 仅唯一普通执行上下文调用；无锁、无堆分配，不允许 ISR 调用。
 */
FlashFTL_StatusTypeDef FlashFTL_Init(FlashFTL_HandleTypeDef *hftl,
                                     const FlashFTL_RawOpsTypeDef *ops,
                                     void *context,
                                     const FlashFTL_MemoryTypeDef *memory)
{
    FlashFTL_RawGeometryTypeDef geometry;
    if (!hftl || !ops || !memory || !ops->GetGeometry || !ops->ReadStart || !ops->ProgramStart ||
        !ops->EraseStart || !ops->Process || !ops->Quiesce || !memory->Map || !memory->Versions ||
        !memory->BlockStates || !memory->Work || !memory->Scratch)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    if (ops->GetGeometry(context, &geometry) != FLASH_FTL_RAW_OK || geometry.EraseBytes != 4096 ||
        geometry.PageBytes != 256 || geometry.SizeBytes % 4096 || geometry.SizeBytes / 4096 < 32)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    uint32_t data_block_count = geometry.SizeBytes / 4096 - 2;
    uint32_t reserve_blocks =
        (uint32_t)(((uint64_t)data_block_count * FLASH_FTL_RESERVE_PERCENT + 99) / 100);
    if (reserve_blocks <= FLASH_FTL_MIN_FREE_BLOCKS || reserve_blocks >= data_block_count ||
        memory->GroupCapacity < data_block_count - reserve_blocks ||
        memory->BlockCapacity < data_block_count ||
        FLASH_FTL_GC_START_RESERVE_PERCENT >= FLASH_FTL_GC_STOP_RESERVE_PERCENT ||
        FLASH_FTL_GC_STOP_RESERVE_PERCENT > 100)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    memset(hftl, 0, sizeof(*hftl));
    hftl->Ops = ops;
    hftl->Context = context;
    hftl->Memory = *memory;
    hftl->Info = (FlashFTL_InfoTypeDef){(data_block_count - reserve_blocks) * 7,
                                        data_block_count - reserve_blocks,
                                        data_block_count,
                                        reserve_blocks};
    hftl->Diagnostics.FormatVersion = FLASH_FTL_FORMAT_VERSION;
    hftl->Diagnostics.Result = FLASH_FTL_NOT_READY;
    return FLASH_FTL_OK;
}

/**
 * @brief 受理只读卷扫描，重建逻辑组映射与物理块状态。
 * @param hftl 已绑定实例；扫描期间表与工作区由本 Module 独占。
 * @retval FLASH_FTL_OK 仅表示受理，调用 Process 直到最终结果。
 * @retval FLASH_FTL_BUSY 已有请求在飞，不改变当前请求。
 * @retval FLASH_FTL_INVALID_PARAM 句柄无效或尚未绑定。
 * @note 不自动格式化。最新已提交记录损坏会报告 CORRUPT，不静默选择旧版。
 *       仅唯一普通执行上下文调用，操作期间不能使用旧 RAM 映射。
 */
FlashFTL_StatusTypeDef FlashFTL_OpenStart(FlashFTL_HandleTypeDef *hftl)
{
    FlashFTL_StatusTypeDef status =
        flash_ftl_start_operation(hftl, FLASH_FTL_OP_OPEN, FLASH_FTL_STEP_VOLUME_A);
    if (status == FLASH_FTL_OK)
    {
        hftl->Ready = false;
    }
    return status;
}

/**
 * @brief 受理显式破坏性底层格式化，建立新格式代次和空闲块。
 * @param hftl 已绑定实例，仅唯一普通执行上下文调用。
 * @retval FLASH_FTL_OK 请求已受理，须持续 Process 取得完成或失败结果。
 * @retval FLASH_FTL_BUSY 已有请求在飞。
 * @retval FLASH_FTL_INVALID_PARAM 句柄无效或尚未绑定。
 * @warning 销毁所绑定分区内数据；调用者必须先注销文件系统并确认分区范围。
 * @note 两份 PREPARING 持久化后才擦数据，最后写 READY。此操作不建立 FAT 文件系统；
 *       失败不承诺保留旧卷，不能由挂载失败路径自动触发。
 */
FlashFTL_StatusTypeDef FlashFTL_FormatStart(FlashFTL_HandleTypeDef *hftl)
{
    FlashFTL_StatusTypeDef status =
        flash_ftl_start_operation(hftl, FLASH_FTL_OP_FORMAT, FLASH_FTL_STEP_VOLUME_A);
    if (status == FLASH_FTL_OK)
    {
        hftl->Ready = false;
    }
    return status;
}

/**
 * @brief 查询已绑定实例的逻辑容量，不访问介质。
 * @param hftl 已绑定实例；不要求卷已打开。
 * @param info 接收逻辑扇区数、逻辑组数、数据物理块数与预留预算。
 * @retval FLASH_FTL_OK 已复制几何信息。
 * @retval FLASH_FTL_INVALID_PARAM 输入或输出指针无效。
 * @note 在唯一普通执行上下文调用，不与状态修改并发。
 */
FlashFTL_StatusTypeDef FlashFTL_GetInfo(const FlashFTL_HandleTypeDef *hftl,
                                        FlashFTL_InfoTypeDef *info)
{
    if (!hftl || !hftl->Ops || !info)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    *info = hftl->Info;
    return FLASH_FTL_OK;
}

/**
 * @brief 受理 count 个逻辑扇区的读取，未映射扇区返回 0xFF。
 * @param hftl 已打开的实例，仅唯一普通执行上下文调用。
 * @param lba 起始逻辑扇区号，不是原始 Flash 地址。
 * @param data 至少 count * 512 字节的输出；直到最终完成或安全收尾前保持有效。
 * @param count 非零扇区数，整个范围须位于逻辑容量内。
 * @retval FLASH_FTL_OK 请求已受理，数据须等 Process 返回成功后再使用。
 * @retval FLASH_FTL_INVALID_PARAM 参数或完整范围无效，不污染卷状态。
 * @retval FLASH_FTL_BUSY 已有操作在飞。
 * @retval FLASH_FTL_NOT_READY 卷尚未成功打开或已故障。
 * @note 用户缓冲仅由 CPU 复制；原始 DMA 使用注入的内部工作区。
 *       失败时输出可能部分更新，不允许将其视为完整有效结果。
 */
FlashFTL_StatusTypeDef FlashFTL_ReadStart(FlashFTL_HandleTypeDef *hftl,
                                          uint32_t lba,
                                          uint8_t *data,
                                          uint32_t count)
{
    if (!hftl || !hftl->Ops || !data || !count || lba >= hftl->Info.SectorCount ||
        count > hftl->Info.SectorCount - lba)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    if (hftl->Active)
    {
        return FLASH_FTL_BUSY;
    }
    if (!hftl->Ready)
    {
        return FLASH_FTL_NOT_READY;
    }
    hftl->Lba = lba;
    hftl->Count = count;
    hftl->Done = 0;
    hftl->ReadBuffer = data;
    return flash_ftl_start_operation(hftl, FLASH_FTL_OP_READ, FLASH_FTL_STEP_READ);
}

/**
 * @brief 有限推进当前请求的软件状态或一笔原始操作，不等待 RTOS。
 * @param hftl 已绑定实例，与 Start/Abort 由同一普通执行上下文调用。
 * @retval FLASH_FTL_RUNNING 软件可继续推进，此时不可开始其他请求。
 * @retval FLASH_FTL_WAIT 原始操作或故障安全收尾尚未完成，稍后重查。
 * @retval FLASH_FTL_OK 当前请求已完成；读数据可用，写请求已逐组提交。
 * @retval FLASH_FTL_INVALID_PARAM 实例无效或未绑定。
 * @retval FLASH_FTL_UNFORMATTED 没有可识别的卷描述，不自动格式化。
 * @retval FLASH_FTL_INCOMPLETE 最新格式代次只有 PREPARING，格式化未完成。
 * @retval FLASH_FTL_INCOMPATIBLE 卷几何/格式不兼容，或代次/组版本不能继续递增。
 * @retval FLASH_FTL_CORRUPT 获选记录、提交关联、数据或擦除回读验证失败。
 * @retval FLASH_FTL_IO_ERROR 原始操作失败或被显式 Abort，已完成安全收尾。
 * @retval FLASH_FTL_NO_SPACE 无法在保护空闲下限以上分配，也无可回收块。
 * @note 无请求在飞时原样返回上次操作结果；刚 Init 后为 FLASH_FTL_NOT_READY。
 * @note 故障先执行 Quiesce。确认控制器/DMA 停止访问前保持操作所有权，
 *       不因超时立即归还缓冲；不是 NOR 内部擦写取消接口。禁止 ISR 调用。
 */
FlashFTL_StatusTypeDef FlashFTL_Process(FlashFTL_HandleTypeDef *hftl)
{
    if (!hftl || !hftl->Ops)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    if (!hftl->Active)
    {
        return hftl->Diagnostics.Result;
    }
    uint8_t *record = hftl->Memory.Work;
    if (hftl->Step == FLASH_FTL_STEP_FAULT)
    {
        FlashFTL_RawStatusTypeDef status = hftl->Ops->Quiesce(hftl->Context);
        if (status != FLASH_FTL_RAW_OK)
        {
            return FLASH_FTL_WAIT;
        }
        return flash_ftl_complete(hftl, hftl->Diagnostics.Result);
    }
    if (hftl->Pending)
    {
        FlashFTL_RawStatusTypeDef status = hftl->Ops->Process(hftl->Context);
        if (status == FLASH_FTL_RAW_BUSY)
        {
            return FLASH_FTL_WAIT;
        }
        hftl->Pending = false;
        hftl->Diagnostics.LastRawStatus = status;
        if (status != FLASH_FTL_RAW_OK)
        {
            return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_IO_ERROR);
        }
        hftl->Step = hftl->NextStep;
    }
    switch (hftl->Step)
    {
        /* 卷描述：读两份代次，区分打开旧卷与显式建立新卷。 */
        case FLASH_FTL_STEP_VOLUME_A:
            return flash_ftl_start_raw_read(hftl, 0, record, 512, FLASH_FTL_STEP_VOLUME_A_DONE);

        case FLASH_FTL_STEP_VOLUME_A_DONE:
            flash_ftl_parse_volume(hftl, 0);
            hftl->Step = FLASH_FTL_STEP_VOLUME_B;
            break;

        case FLASH_FTL_STEP_VOLUME_B:
            return flash_ftl_start_raw_read(hftl, 4096, record, 512, FLASH_FTL_STEP_VOLUME_B_DONE);

        case FLASH_FTL_STEP_VOLUME_B_DONE:
            flash_ftl_parse_volume(hftl, 1);
            hftl->Step = FLASH_FTL_STEP_SELECT_VOLUME;
            break;

        case FLASH_FTL_STEP_SELECT_VOLUME:
        {
            uint32_t latest = hftl->VolumeEpoch[1] > hftl->VolumeEpoch[0] ? 1 : 0;
            hftl->Epoch = hftl->VolumeEpoch[latest];
            if (hftl->Operation == FLASH_FTL_OP_FORMAT)
            {
                if (hftl->Epoch == UINT64_MAX)
                {
                    return flash_ftl_complete(hftl, FLASH_FTL_INCOMPATIBLE);
                }
                /* 先改另一份，保留最新有效代次；A/B 是角色，不固定破坏唯一有效 B。 */
                hftl->FirstVolume = 1 - latest;
                hftl->Target = hftl->FirstVolume;
                hftl->Page = 0;
                ++hftl->Epoch;
                flash_ftl_clear_tables(hftl);
                hftl->Step = FLASH_FTL_STEP_FORMAT_ERASE_VOLUME;
                break;
            }
            if (!hftl->Epoch)
            {
                return flash_ftl_complete(hftl, FLASH_FTL_UNFORMATTED);
            }
            bool ready = false;
            for (uint32_t i = 0; i < 2; i++)
            {
                if (hftl->VolumeEpoch[i] == hftl->Epoch)
                {
                    if (!hftl->VolumeCompatible[i])
                    {
                        return flash_ftl_complete(hftl, FLASH_FTL_INCOMPATIBLE);
                    }
                    if (hftl->VolumePhase[i] == FLASH_FTL_READY)
                    {
                        ready = true;
                    }
                }
            }
            if (!ready)
            {
                return flash_ftl_complete(hftl, FLASH_FTL_INCOMPLETE);
            }
            flash_ftl_clear_tables(hftl);
            hftl->Index = 0;
            hftl->Step = FLASH_FTL_STEP_SCAN;
            break;
        }
        /* 格式化：双 PREPARING -> 数据块全擦并验证 -> 双 READY。 */
        case FLASH_FTL_STEP_FORMAT_ERASE_VOLUME:
            return flash_ftl_start_raw_erase(
                hftl, hftl->Target * 4096, FLASH_FTL_STEP_FORMAT_VERIFY_VOLUME);

        case FLASH_FTL_STEP_FORMAT_VERIFY_VOLUME:
            return flash_ftl_start_raw_read(
                hftl, hftl->Target * 4096, record, 4096, FLASH_FTL_STEP_FORMAT_CHECK_VOLUME);

        case FLASH_FTL_STEP_FORMAT_CHECK_VOLUME:
            /* 不把旧 READY 页残留到下一代，也不在未完全擦除的页上重复编程。 */
            if (!flash_ftl_is_erased(record, 4096))
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            hftl->Step = FLASH_FTL_STEP_FORMAT_PREPARE;
            break;

        case FLASH_FTL_STEP_FORMAT_PREPARE:
            flash_ftl_encode_volume(hftl, FLASH_FTL_PREPARING);
            return flash_ftl_start_raw_program(
                hftl, hftl->Target * 4096, record, FLASH_FTL_STEP_FORMAT_VERIFY_PREPARE);

        case FLASH_FTL_STEP_FORMAT_VERIFY_PREPARE:
            return flash_ftl_start_raw_read(hftl,
                                            hftl->Target * 4096,
                                            hftl->Memory.Scratch,
                                            256,
                                            FLASH_FTL_STEP_FORMAT_CHECK_PREPARE);

        case FLASH_FTL_STEP_FORMAT_CHECK_PREPARE:
            if (memcmp(record, hftl->Memory.Scratch, 256))
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            if (hftl->Page++ == 0)
            {
                hftl->Target = 1 - hftl->Target;
                hftl->Step = FLASH_FTL_STEP_FORMAT_ERASE_VOLUME;
            }
            else
            {
                hftl->Index = 0;
                hftl->Step = FLASH_FTL_STEP_FORMAT_ERASE_DATA;
            }
            break;

        case FLASH_FTL_STEP_FORMAT_ERASE_DATA:
            if (hftl->Index == hftl->Info.DataBlockCount)
            {
                hftl->Page = 0;
                hftl->Target = hftl->FirstVolume;
                hftl->Step = FLASH_FTL_STEP_FORMAT_READY;
                break;
            }
            return flash_ftl_start_raw_erase(
                hftl, flash_ftl_data_block_address(hftl->Index), FLASH_FTL_STEP_FORMAT_VERIFY_DATA);

        case FLASH_FTL_STEP_FORMAT_VERIFY_DATA:
            return flash_ftl_start_raw_read(hftl,
                                            flash_ftl_data_block_address(hftl->Index),
                                            record,
                                            4096,
                                            FLASH_FTL_STEP_FORMAT_CHECK_DATA);

        case FLASH_FTL_STEP_FORMAT_CHECK_DATA:
            if (!flash_ftl_is_erased(record, 4096))
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            hftl->Memory.BlockStates[hftl->Index++] = FLASH_FTL_BLOCK_FREE;
            ++hftl->FreeBlocks;
            ++hftl->Diagnostics.SessionErases;
            hftl->Step = FLASH_FTL_STEP_FORMAT_ERASE_DATA;
            break;

        case FLASH_FTL_STEP_FORMAT_READY:
            flash_ftl_encode_volume(hftl, FLASH_FTL_READY);
            return flash_ftl_start_raw_program(
                hftl, hftl->Target * 4096 + 256, record, FLASH_FTL_STEP_FORMAT_VERIFY_READY);

        case FLASH_FTL_STEP_FORMAT_VERIFY_READY:
            return flash_ftl_start_raw_read(hftl,
                                            hftl->Target * 4096 + 256,
                                            hftl->Memory.Scratch,
                                            256,
                                            FLASH_FTL_STEP_FORMAT_CHECK_READY);

        case FLASH_FTL_STEP_FORMAT_CHECK_READY:
            if (memcmp(record, hftl->Memory.Scratch, 256))
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            if (hftl->Page++ == 0)
            {
                hftl->Target = 1 - hftl->Target;
                hftl->Step = FLASH_FTL_STEP_FORMAT_READY;
            }
            else
            {
                hftl->Ready = true;
                return flash_ftl_complete(hftl, FLASH_FTL_OK);
            }
            break;
        /* 恢复第一遍只选择提交版本；第二遍完整校验获选记录。 */
        case FLASH_FTL_STEP_SCAN:
            if (hftl->Index == hftl->Info.DataBlockCount)
            {
                hftl->Group = 0;
                hftl->Step = FLASH_FTL_STEP_VALIDATE;
                break;
            }
            return flash_ftl_start_raw_read(hftl,
                                            flash_ftl_data_block_address(hftl->Index),
                                            record,
                                            256,
                                            FLASH_FTL_STEP_SCAN_FOOTER);

        case FLASH_FTL_STEP_SCAN_FOOTER:
            return flash_ftl_start_raw_read(hftl,
                                            flash_ftl_data_block_address(hftl->Index) +
                                                FLASH_FTL_COMMIT_OFFSET,
                                            record + FLASH_FTL_COMMIT_OFFSET,
                                            256,
                                            FLASH_FTL_STEP_SCAN_DONE);

        case FLASH_FTL_STEP_SCAN_DONE:
            hftl->Memory.BlockStates[hftl->Index] = FLASH_FTL_BLOCK_STALE;
            if (flash_ftl_is_erased(record, 256) &&
                flash_ftl_is_erased(record + FLASH_FTL_COMMIT_OFFSET, 256))
            {
                return flash_ftl_start_raw_read(hftl,
                                                flash_ftl_data_block_address(hftl->Index),
                                                record,
                                                4096,
                                                FLASH_FTL_STEP_SCAN_CHECK_EMPTY);
            }
            if (flash_ftl_commit_is_valid(record))
            {
                /* GC 可先擦坏旧头而保留旧提交页。先依据提交页选最新版，
                 * 再在第二遍校验获选记录，避免把已失效块的撕裂擦除当成卷损坏。 */
                const uint8_t *commit = record + FLASH_FTL_COMMIT_OFFSET;
                if (flash_ftl_read_u64(commit + 8) == hftl->Epoch)
                {
                    uint32_t group = flash_ftl_read_u32(commit + 16);
                    uint64_t version = flash_ftl_read_u64(commit + 20);
                    if (group >= hftl->Info.GroupCount || !version)
                    {
                        return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
                    }
                    if (version == hftl->Memory.Versions[group])
                    {
                        return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
                    }
                    if (version > hftl->Memory.Versions[group])
                    {
                        uint32_t previous = hftl->Memory.Map[group];
                        if (previous != FLASH_FTL_UNMAPPED)
                        {
                            hftl->Memory.BlockStates[previous] = FLASH_FTL_BLOCK_STALE;
                        }
                        hftl->Memory.Map[group] = hftl->Index;
                        hftl->Memory.Versions[group] = version;
                        hftl->Memory.BlockStates[hftl->Index] = FLASH_FTL_BLOCK_VALID;
                    }
                }
            }
            ++hftl->Index;
            hftl->Step = FLASH_FTL_STEP_SCAN;
            break;

        case FLASH_FTL_STEP_SCAN_CHECK_EMPTY:
            if (flash_ftl_is_erased(record, 4096))
            {
                hftl->Memory.BlockStates[hftl->Index] = FLASH_FTL_BLOCK_FREE;
                ++hftl->FreeBlocks;
            }
            ++hftl->Index;
            hftl->Step = FLASH_FTL_STEP_SCAN;
            break;

        case FLASH_FTL_STEP_VALIDATE:
            if (hftl->Group == hftl->Info.GroupCount)
            {
                hftl->Ready = true;
                return flash_ftl_complete(hftl, FLASH_FTL_OK);
            }
            if (hftl->Memory.Map[hftl->Group] == FLASH_FTL_UNMAPPED)
            {
                ++hftl->Group;
                break;
            }
            return flash_ftl_start_raw_read(
                hftl,
                flash_ftl_data_block_address(hftl->Memory.Map[hftl->Group]),
                record,
                4096,
                FLASH_FTL_STEP_VALIDATE_DONE);

        case FLASH_FTL_STEP_VALIDATE_DONE:
            if (!flash_ftl_record_data_is_valid(record) ||
                flash_ftl_read_u64(record + 8) != hftl->Epoch ||
                flash_ftl_read_u32(record + 16) != hftl->Group ||
                flash_ftl_read_u64(record + 20) != hftl->Memory.Versions[hftl->Group])
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            ++hftl->Group;
            hftl->Step = FLASH_FTL_STEP_VALIDATE;
            break;
        /* 逻辑访问：每次处理一个逻辑组，跨组进度仅在本请求内累计。 */
        case FLASH_FTL_STEP_READ:
            if (hftl->Done == hftl->Count)
            {
                return flash_ftl_complete(hftl, FLASH_FTL_OK);
            }
            flash_ftl_select_request_group(hftl);
            if (hftl->Memory.Map[hftl->Group] == FLASH_FTL_UNMAPPED)
            {
                memset(hftl->ReadBuffer + hftl->Done * 512, 255, hftl->Chunk * 512);
                hftl->Done += hftl->Chunk;
                break;
            }
            return flash_ftl_start_raw_read(
                hftl,
                flash_ftl_data_block_address(hftl->Memory.Map[hftl->Group]),
                record,
                4096,
                FLASH_FTL_STEP_READ_DONE);

        case FLASH_FTL_STEP_READ_DONE:
            if (!flash_ftl_record_data_is_valid(record) ||
                flash_ftl_read_u64(record + 8) != hftl->Epoch ||
                flash_ftl_read_u32(record + 16) != hftl->Group ||
                flash_ftl_read_u64(record + 20) != hftl->Memory.Versions[hftl->Group])
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            memcpy(hftl->ReadBuffer + hftl->Done * 512,
                   record + 256 + (hftl->Lba + hftl->Done) % 7 * 512,
                   hftl->Chunk * 512);
            hftl->Done += hftl->Chunk;
            hftl->Step = FLASH_FTL_STEP_READ;
            break;

        case FLASH_FTL_STEP_WRITE:
            if (hftl->Done == hftl->Count)
            {
                return flash_ftl_complete(hftl, FLASH_FTL_OK);
            }
            flash_ftl_select_request_group(hftl);
            hftl->OldBlock = hftl->Memory.Map[hftl->Group];
            if (hftl->Memory.Versions[hftl->Group] == UINT64_MAX)
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_INCOMPATIBLE);
            }
            if (hftl->OldBlock != FLASH_FTL_UNMAPPED && hftl->Chunk != 7)
            {
                return flash_ftl_start_raw_read(hftl,
                                                flash_ftl_data_block_address(hftl->OldBlock),
                                                record,
                                                4096,
                                                FLASH_FTL_STEP_WRITE_OLD_DONE);
            }
            memset(record, 255, 4096);
            hftl->Step = FLASH_FTL_STEP_WRITE_ALLOCATE;
            break;

        case FLASH_FTL_STEP_WRITE_OLD_DONE:
            if (!flash_ftl_record_data_is_valid(record) ||
                flash_ftl_read_u64(record + 8) != hftl->Epoch ||
                flash_ftl_read_u32(record + 16) != hftl->Group ||
                flash_ftl_read_u64(record + 20) != hftl->Memory.Versions[hftl->Group])
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            hftl->Step = FLASH_FTL_STEP_WRITE_ALLOCATE;
            break;

        case FLASH_FTL_STEP_WRITE_ALLOCATE:
        {
            if (hftl->FreeBlocks <= FLASH_FTL_MIN_FREE_BLOCKS)
            {
                hftl->GcForWrite = true;
                hftl->Step = FLASH_FTL_STEP_GC;
                break;
            }
            uint32_t b = hftl->AllocationCursor;
            for (uint32_t i = 0; i < hftl->Info.DataBlockCount;
                 i++, b = (b + 1) % hftl->Info.DataBlockCount)
            {
                if (hftl->Memory.BlockStates[b] != FLASH_FTL_BLOCK_FREE)
                {
                    continue;
                }
                hftl->Target = b;
                hftl->AllocationCursor = (b + 1) % hftl->Info.DataBlockCount;
                break;
            }
            hftl->Memory.BlockStates[hftl->Target] = FLASH_FTL_BLOCK_STALE;
            --hftl->FreeBlocks;
            memcpy(record + 256 + (hftl->Lba + hftl->Done) % 7 * 512,
                   hftl->WriteBuffer + hftl->Done * 512,
                   hftl->Chunk * 512);
            hftl->NewVersion = hftl->Memory.Versions[hftl->Group] + 1;
            flash_ftl_encode_record(hftl);
            hftl->Page = 0;
            hftl->Step = FLASH_FTL_STEP_WRITE_PAGE;
            break;
        }

        case FLASH_FTL_STEP_WRITE_PAGE:
            if (hftl->Page == 15)
            {
                hftl->Page = 0;
                hftl->Step = FLASH_FTL_STEP_WRITE_VERIFY;
                break;
            }
            if (hftl->Page > 0 && flash_ftl_is_erased(record + hftl->Page * 256, 256))
            {
                ++hftl->Page;
                break;
            }
            {
                uint32_t page = hftl->Page++;
                return flash_ftl_start_raw_program(hftl,
                                                   flash_ftl_data_block_address(hftl->Target) +
                                                       page * 256,
                                                   record + page * 256,
                                                   FLASH_FTL_STEP_WRITE_PAGE);
            }

        case FLASH_FTL_STEP_WRITE_VERIFY:
            /* 提交前逐段比对，Scratch 不覆盖待写提交页。 */
            if (hftl->Page == 15)
            {
                hftl->Step = FLASH_FTL_STEP_WRITE_COMMIT;
                break;
            }
            return flash_ftl_start_raw_read(hftl,
                                            flash_ftl_data_block_address(hftl->Target) +
                                                hftl->Page * 256,
                                            hftl->Memory.Scratch,
                                            256,
                                            FLASH_FTL_STEP_WRITE_VERIFY_DONE);

        case FLASH_FTL_STEP_WRITE_VERIFY_DONE:
            if (memcmp(record + hftl->Page * 256, hftl->Memory.Scratch, 256))
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            ++hftl->Page;
            hftl->Step = FLASH_FTL_STEP_WRITE_VERIFY;
            break;

        case FLASH_FTL_STEP_WRITE_COMMIT:
            return flash_ftl_start_raw_program(hftl,
                                               flash_ftl_data_block_address(hftl->Target) +
                                                   FLASH_FTL_COMMIT_OFFSET,
                                               record + FLASH_FTL_COMMIT_OFFSET,
                                               FLASH_FTL_STEP_WRITE_COMMIT_VERIFY);

        case FLASH_FTL_STEP_WRITE_COMMIT_VERIFY:
            return flash_ftl_start_raw_read(hftl,
                                            flash_ftl_data_block_address(hftl->Target) +
                                                FLASH_FTL_COMMIT_OFFSET,
                                            hftl->Memory.Scratch,
                                            256,
                                            FLASH_FTL_STEP_WRITE_COMMIT_DONE);

        case FLASH_FTL_STEP_WRITE_COMMIT_DONE:
            if (memcmp(record + FLASH_FTL_COMMIT_OFFSET, hftl->Memory.Scratch, 256))
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            /* 唯一 RAM 映射切换点：新提交已读回验证，此前旧版本始终保留。 */
            hftl->Memory.Map[hftl->Group] = hftl->Target;
            hftl->Memory.Versions[hftl->Group] = hftl->NewVersion;
            hftl->Memory.BlockStates[hftl->Target] = FLASH_FTL_BLOCK_VALID;
            if (hftl->OldBlock != FLASH_FTL_UNMAPPED)
            {
                hftl->Memory.BlockStates[hftl->OldBlock] = FLASH_FTL_BLOCK_STALE;
            }
            hftl->Done += hftl->Chunk;
            hftl->Step = FLASH_FTL_STEP_WRITE;
            break;

        case FLASH_FTL_STEP_SYNC:
            return flash_ftl_complete(hftl, FLASH_FTL_OK);

        case FLASH_FTL_STEP_GC:
        {
            if (!hftl->GcForWrite)
            {
                uint32_t low =
                    (hftl->Info.ReserveBlocks * FLASH_FTL_GC_START_RESERVE_PERCENT + 99) / 100;
                uint32_t high =
                    (hftl->Info.ReserveBlocks * FLASH_FTL_GC_STOP_RESERVE_PERCENT + 99) / 100;
                if (hftl->FreeBlocks <= low)
                {
                    hftl->GcRequested = true;
                }
                if (hftl->FreeBlocks >= high)
                {
                    hftl->GcRequested = false;
                }
                if (!hftl->GcRequested)
                {
                    return flash_ftl_complete(hftl, FLASH_FTL_OK);
                }
            }
            uint32_t b = hftl->GcCursor;
            bool found = false;
            for (uint32_t i = 0; i < hftl->Info.DataBlockCount;
                 i++, b = (b + 1) % hftl->Info.DataBlockCount)
            {
                if (hftl->Memory.BlockStates[b] != FLASH_FTL_BLOCK_STALE)
                {
                    continue;
                }
                hftl->Index = b;
                hftl->GcCursor = (b + 1) % hftl->Info.DataBlockCount;
                found = true;
                break;
            }
            if (!found)
            {
                return flash_ftl_complete(hftl,
                                          hftl->GcForWrite ? FLASH_FTL_NO_SPACE : FLASH_FTL_OK);
            }
            hftl->Page = 0;
            return flash_ftl_start_raw_erase(
                hftl, flash_ftl_data_block_address(hftl->Index), FLASH_FTL_STEP_GC_VERIFY);
        }
        /* 前台 GC 复用本阶段；用 Scratch 校验，保留 Work 内待提交的组。 */
        case FLASH_FTL_STEP_GC_VERIFY:
            return flash_ftl_start_raw_read(hftl,
                                            flash_ftl_data_block_address(hftl->Index) +
                                                hftl->Page * 512,
                                            hftl->Memory.Scratch,
                                            512,
                                            FLASH_FTL_STEP_GC_DONE);

        case FLASH_FTL_STEP_GC_DONE:
            if (!flash_ftl_is_erased(hftl->Memory.Scratch, 512))
            {
                return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
            }
            if (++hftl->Page < 8)
            {
                hftl->Step = FLASH_FTL_STEP_GC_VERIFY;
                break;
            }
            hftl->Memory.BlockStates[hftl->Index] = FLASH_FTL_BLOCK_FREE;
            ++hftl->FreeBlocks;
            ++hftl->Diagnostics.SessionErases;
            if (hftl->GcForWrite)
            {
                hftl->Step = FLASH_FTL_STEP_WRITE_ALLOCATE;
                break;
            }
            return flash_ftl_complete(hftl, FLASH_FTL_OK);

        default:
            return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_CORRUPT);
    }
    return FLASH_FTL_RUNNING;
}

/**
 * @brief 受理逻辑扇区写入，整组异地提交并验证后才确认完成。
 * @param hftl 已打开实例，仅唯一普通执行上下文调用。
 * @param lba 起始逻辑扇区号。
 * @param data 至少 count * 512 字节的输入，最终完成或安全收尾前保持有效且不可改写。
 * @param count 非零扇区数；整个范围须位于逻辑容量内。
 * @retval FLASH_FTL_OK 仅表示受理，真实完成由 Process 返回。
 * @retval FLASH_FTL_INVALID_PARAM 参数或范围无效，不污染卷状态。
 * @retval FLASH_FTL_BUSY 已有操作在飞。
 * @retval FLASH_FTL_NOT_READY 卷未就绪。
 * @note 无 RAM 写回早确认。跨组请求不保证整体原子，失败前的部分组可能已提交；
 *       用户数据通过 CPU 复制，DMA 条件仅约束注入的内部工作区。
 */
FlashFTL_StatusTypeDef FlashFTL_WriteStart(FlashFTL_HandleTypeDef *hftl,
                                           uint32_t lba,
                                           const uint8_t *data,
                                           uint32_t count)
{
    if (!hftl || !hftl->Ops || !data || !count || lba >= hftl->Info.SectorCount ||
        count > hftl->Info.SectorCount - lba)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    if (hftl->Active)
    {
        return FLASH_FTL_BUSY;
    }
    if (!hftl->Ready)
    {
        return FLASH_FTL_NOT_READY;
    }
    hftl->Lba = lba;
    hftl->Count = count;
    hftl->Done = 0;
    hftl->WriteBuffer = data;
    return flash_ftl_start_operation(hftl, FLASH_FTL_OP_WRITE, FLASH_FTL_STEP_WRITE);
}

/**
 * @brief 受理一次有限回收，由 GC 水位决定是否回收失效块。
 * @param hftl 已打开实例，只允许唯一普通执行上下文调用。
 * @retval FLASH_FTL_OK 已受理；Process 可能不擦除，也可能最多回收一个块。
 * @retval FLASH_FTL_BUSY 已有请求在飞。
 * @retval FLASH_FTL_NOT_READY 卷未就绪。
 * @retval FLASH_FTL_INVALID_PARAM 句柄无效或未绑定。
 * @note 不擦当前有效块，不保证已开始的 NOR 擦除可抢占。
 */
FlashFTL_StatusTypeDef FlashFTL_ReclaimStart(FlashFTL_HandleTypeDef *hftl)
{
    if (!hftl || !hftl->Ops)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    if (hftl->Active)
    {
        return FLASH_FTL_BUSY;
    }
    if (!hftl->Ready)
    {
        return FLASH_FTL_NOT_READY;
    }
    hftl->GcForWrite = false;
    return flash_ftl_start_operation(hftl, FLASH_FTL_OP_GC, FLASH_FTL_STEP_GC);
}

/**
 * @brief 查询卷是否已打开且当前无在飞请求，不访问硬件。
 * @param hftl 实例，允许 NULL。
 * @retval true 可受理新的逻辑请求。
 * @retval false 未绑定、未打开、故障或当前正忙。
 * @note 在唯一普通执行上下文查询，不作为跨任务同步原语。
 */
bool FlashFTL_IsReady(const FlashFTL_HandleTypeDef *hftl)
{
    return hftl && hftl->Ops && hftl->Ready && !hftl->Active;
}

/**
 * @brief 取得卷和块状态的诊断快照，不输出日志或访问介质。
 * @param hftl 已绑定实例，必须与操作推进处于同一普通执行上下文。
 * @param diagnostics 接收格式版本、epoch、状态与块统计。
 * @retval FLASH_FTL_OK 已生成快照。
 * @retval FLASH_FTL_INVALID_PARAM 输入或输出指针无效。
 * @note SessionErases 是本次绑定以来数据块擦除累计数，不含卷头；
 *       其他块计数反映当前 RAM 表，未就绪时 ValidGroups/StaleBlocks 为零。
 *       统计不持久化，也不能用于承诺剩余寿命。
 */
FlashFTL_StatusTypeDef FlashFTL_GetDiagnostics(const FlashFTL_HandleTypeDef *hftl,
                                               FlashFTL_DiagnosticsTypeDef *diagnostics)
{
    if (!hftl || !hftl->Ops || !diagnostics)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    *diagnostics = hftl->Diagnostics;
    diagnostics->Epoch = hftl->Epoch;
    diagnostics->FreeBlocks = hftl->FreeBlocks;
    diagnostics->ValidGroups = 0;
    diagnostics->StaleBlocks = 0;
    if (hftl->Ready)
    {
        for (uint32_t i = 0; i < hftl->Info.DataBlockCount; i++)
        {
            if (hftl->Memory.BlockStates[i] == FLASH_FTL_BLOCK_VALID)
            {
                ++diagnostics->ValidGroups;
            }
            if (hftl->Memory.BlockStates[i] == FLASH_FTL_BLOCK_STALE)
            {
                ++diagnostics->StaleBlocks;
            }
        }
    }
    return FLASH_FTL_OK;
}

/**
 * @brief 受理同步确认，首版没有待刷新的 RAM 写回缓存。
 * @param hftl 已打开且无其他操作在飞的实例。
 * @retval FLASH_FTL_OK 已受理，继续 Process 确认最终结果。
 * @retval FLASH_FTL_BUSY 已有请求在飞，不能用 Sync 替代其 Process。
 * @retval FLASH_FTL_NOT_READY 卷未就绪。
 * @retval FLASH_FTL_INVALID_PARAM 实例无效或未绑定。
 * @note 仅唯一普通上下文调用，不强制清空全部后台 GC。
 */
FlashFTL_StatusTypeDef FlashFTL_SyncStart(FlashFTL_HandleTypeDef *hftl)
{
    if (!hftl || !hftl->Ops)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    if (hftl->Active)
    {
        return FLASH_FTL_BUSY;
    }
    if (!hftl->Ready)
    {
        return FLASH_FTL_NOT_READY;
    }
    return flash_ftl_start_operation(hftl, FLASH_FTL_OP_SYNC, FLASH_FTL_STEP_SYNC);
}

/**
 * @brief 请求当前操作进入失败收尾，不立即归还缓冲或回滚数据。
 * @param hftl 当前有请求在飞的已绑定实例。
 * @retval FLASH_FTL_RUNNING 已进入故障收尾，仍须继续调用 Process。
 * @retval FLASH_FTL_NOT_READY 当前没有在飞请求。
 * @retval FLASH_FTL_INVALID_PARAM 实例无效或未绑定。
 * @note 仅唯一普通上下文调用，禁止 ISR 使用；最终结束后需显式恢复/重扫，
 *       已完成提交的组不会因 Abort 撤销。
 */
FlashFTL_StatusTypeDef FlashFTL_Abort(FlashFTL_HandleTypeDef *hftl)
{
    if (!hftl || !hftl->Ops)
    {
        return FLASH_FTL_INVALID_PARAM;
    }
    if (!hftl->Active)
    {
        return FLASH_FTL_NOT_READY;
    }
    return flash_ftl_begin_fault_cleanup(hftl, FLASH_FTL_IO_ERROR);
}
