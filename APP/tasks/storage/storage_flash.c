/**
 * @file storage_flash.c
 * @brief APP 诊断编排的薄转发；Flash 回调与等待统一归 Filesystem Service。
 */

#include "APP/tasks/storage/storage_flash.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/log/log_service.h"

/**
 * @brief 在唯一 Storage Task 初始化 Service 执行器，不创建第二条事件订阅。
 * @param task_handle 当前 Storage Task 句柄，必须与实际调用任务一致。
 * @note 仅启动阶段普通上下文调用；失败投递日志，不格式化或访问逻辑卷。
 */
void storage_flash_init(TaskHandle_t task_handle)
{
    if (!task_handle || task_handle != xTaskGetCurrentTaskHandle() ||
        Service_Filesystem_InitFlash() != SERVICE_OK)
    {
        (void)Service_Log_Post(
            SERVICE_LOG_LEVEL_ERROR, "FLASH", "Flash executor initialization failed.");
    }
}

/**
 * @brief 将启动诊断的原始数组读取交给 Filesystem Service 同步执行。
 * @param address 原始字节地址，须满足 Platform 范围与对齐约束。
 * @param data 满足 Platform DMA 条件的接收缓冲。
 * @param data_length 非零读取字节数。
 * @param timeout_ms 非零毫秒预算，到期后 Service 仍须完成安全收尾。
 * @return true 为完成，false 为失败；仅 Storage Task 调用，返回前不复用缓冲。
 */
bool storage_flash_read_array(uint32_t address,
                              uint8_t *data,
                              uint32_t data_length,
                              uint32_t timeout_ms)
{
    return Service_Filesystem_ReadFlashArray(address, data, data_length, timeout_ms);
}

/**
 * @brief 同步读回保留自检扇区，不操作 FTL 分区。
 * @param region 首或尾自检区。
 * @param data 满足 Platform DMA 条件的 4 KiB 接收缓冲。
 * @param data_length 必须为一个完整自检扇区的字节数。
 * @param timeout_ms 非零毫秒预算。
 * @return true 为完成，false 为失败；仅 Storage Task 启动诊断独占期调用。
 */
bool storage_flash_read_diagnostic(Platform_Flash_DiagnosticRegionTypeDef region,
                                   uint8_t *data,
                                   uint32_t data_length,
                                   uint32_t timeout_ms)
{
    return Service_Filesystem_ReadFlashDiagnostic(region, data, data_length, timeout_ms);
}

/**
 * @brief 请求 Service 同步擦除一个保留自检扇区。
 * @param region 首或尾自检区。
 * @param timeout_ms 非零毫秒预算，失败不代表 NOR 内部擦除已取消。
 * @return true 为完成，false 为失败。
 * @warning 仅 Storage Task 显式破坏性诊断调用，不用于逻辑盘或资源数据。
 */
bool storage_flash_erase_diagnostic(Platform_Flash_DiagnosticRegionTypeDef region,
                                    uint32_t timeout_ms)
{
    return Service_Filesystem_EraseFlashDiagnostic(region, timeout_ms);
}

/**
 * @brief 请求 Service 同步编程保留自检区的一页。
 * @param region 首或尾自检区。
 * @param page_offset 扇区内 256 B 对齐页偏移。
 * @param data 返回前保持有效且不可改写的输入。
 * @param data_length 1..256 B，不能跨页。
 * @param timeout_ms 非零毫秒预算。
 * @return true 为完成，false 为失败；仅 Storage Task 启动诊断独占期调用。
 * @warning 目标页必须已擦除，不可通过此入口访问 FTL 分区。
 */
bool storage_flash_program_diagnostic_page(Platform_Flash_DiagnosticRegionTypeDef region,
                                           uint32_t page_offset,
                                           const uint8_t *data,
                                           uint32_t data_length,
                                           uint32_t timeout_ms)
{
    return Service_Filesystem_ProgramFlashDiagnostic(
        region, page_offset, data, data_length, timeout_ms);
}
