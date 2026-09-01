/**
 * @file filesystem_flash_access.c
 * @brief 启动诊断的同步包装；经唯一 Flash 执行器等待并安全收尾。
 */

#include "Service/filesystem/filesystem_flash_access.h"
#include "Service/filesystem/flash/filesystem_flash_transfer.h"

/**
 * @brief 同步执行启动诊断的原始数组读取，不用于逻辑卷访问。
 * @param address 芯片绝对字节地址，满足 Platform 原始读取的范围与对齐约束。
 * @param data DMA 接收缓冲，满足 Platform 的可达性、对齐及 Cache line 独占条件。
 * @param data_length 读取字节数，不为零，完整范围位于芯片内。
 * @param timeout_ms 非零毫秒预算；到期触发安全收尾，不立即释放缓冲。
 * @return SERVICE_OK 表示读取和 Cache 收尾完成；NOT_READY 表示非所有者或超时预算为零；
 *         其余为启动、传输或收尾失败，输出不可视为有效。
 * @note 仅 Storage Task 启动诊断独占期调用；直到返回前缓冲保持有效。
 */
Service_StatusTypeDef Service_Filesystem_ReadFlashArray(uint32_t address,
                                                       uint8_t *data,
                                                       uint32_t data_length,
                                                       uint32_t timeout_ms)
{
    if (!filesystem_flash_transfer_is_owner() || !timeout_ms)
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(
        Platform_Flash_StartReadArray(address, data, data_length), timeout_ms);
}

/**
 * @brief 同步读取 ADR-0009 保留自检扇区。
 * @param region 首或尾自检区域，不接受任意物理写入分区。
 * @param data 满足 Platform DMA 条件的 4 KiB 接收缓冲。
 * @param data_length 必须为一个自检扇区的字节数。
 * @param timeout_ms 非零毫秒预算，超时后仍等待安全收尾。
 * @return SERVICE_OK 表示已完成读取与 Cache 收尾；失败时数据不可用。
 * @note 仅 Storage Task 启动诊断独占期调用；返回前不能复用缓冲。
 */
Service_StatusTypeDef Service_Filesystem_ReadFlashDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms)
{
    if (!filesystem_flash_transfer_is_owner() || !timeout_ms)
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(
        Platform_Flash_StartDiagnosticRead(region, data, data_length), timeout_ms);
}

/**
 * @brief 同步擦除一个保留自检扇区，不提供任意地址擦除。
 * @param region 首或尾自检区域，禁止用来操作 FTL 分区。
 * @param timeout_ms 非零毫秒预算，超时不代表 NOR 内部擦除已取消。
 * @return SERVICE_OK 表示擦除完成且 Platform 已成功收尾。
 * @warning 破坏目标自检区内容，仅 Storage Task 显式启动诊断调用。
 */
Service_StatusTypeDef Service_Filesystem_EraseFlashDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region, uint32_t timeout_ms)
{
    if (!filesystem_flash_transfer_is_owner() || !timeout_ms)
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_StartDiagnosticErase(region),
                                            timeout_ms);
}

/**
 * @brief 同步编程保留自检区的一个已擦除物理页。
 * @param region 首或尾自检区域。
 * @param page_offset 相对扇区首地址的偏移，按 256 B 页对齐。
 * @param data 返回前保持有效且不改写的输入缓冲。
 * @param data_length 1..256 B，不得跨越目标页。
 * @param timeout_ms 非零毫秒预算，到期后继续安全收尾。
 * @return SERVICE_OK 表示编程完成且 Platform 已成功收尾；失败不代表数据一定未写入。
 * @warning 仅 Storage Task 显式启动诊断调用，目标页须先擦除，不操作 FTL。
 */
Service_StatusTypeDef Service_Filesystem_ProgramFlashDiagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t page_offset,
    const uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms)
{
    if (!filesystem_flash_transfer_is_owner() || !timeout_ms)
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(
        Platform_Flash_StartDiagnosticPageProgram(region, page_offset, data, data_length),
        timeout_ms);
}
