/**
 * @file filesystem_flash_transfer.c
 * @brief StorageTask 上下文的 Flash 同步执行、通知等待和安全收尾。
 */

#include "Service/filesystem/flash/filesystem_flash_transfer.h"
#include "Service/filesystem/flash/filesystem_flash_config.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/log/log_service.h"
#include <stdio.h>

static TaskHandle_t filesystem_flash_owner;
static bool filesystem_flash_registered;
static uint32_t filesystem_flash_notify_index;

/**
 * @brief 发布轻量唤醒提示，不在 ISR 中推进 Flash 请求。
 * @param event Platform 已归一化的事件，只作为任务重查提示。
 * @param context 初始化时注册的唯一所有者任务句柄。
 * @note 通知可合并或延迟，真正完成由任务中的 Process 判定；不访问数据缓冲。
 */
static void filesystem_flash_irq(Platform_Flash_OperationEventTypeDef event, void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)xTaskNotifyIndexedFromISR((TaskHandle_t)context,
                                    (UBaseType_t)filesystem_flash_notify_index,
                                    (uint32_t)event,
                                    eSetValueWithOverwrite,
                                    &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief 判断当前任务是否是已注册的 Flash 唯一执行者。
 * @return true 表示已注册且当前任务匹配；false 表示未初始化或调用者不匹配。
 * @note 仅普通任务上下文调用，不做锁仲裁，也不推进硬件。
 */
bool filesystem_flash_transfer_is_owner(void)
{
    return filesystem_flash_registered && filesystem_flash_owner &&
           xTaskGetCurrentTaskHandle() == filesystem_flash_owner;
}

/**
 * @brief 将当前任务登记为 Flash 执行器并注册长期事件回调。
 * @param[in] notify_index 本任务通知数组中的 QSPI/MDMA 完成槽，由 APP 枚举注入。
 * @return true 表示首次注册成功或同一所有者重复初始化；false 表示无任务、索引越界、注册失败或所有者不匹配。
 * @note 由 Storage Task 初始化；注册成功后不临时转移回调、任务所有权和通知槽。
 */
bool filesystem_flash_transfer_init(uint32_t notify_index)
{
    if (notify_index >= (uint32_t)configTASK_NOTIFICATION_ARRAY_ENTRIES)
    {
        return false;
    }
    if (filesystem_flash_registered)
    {
        return filesystem_flash_transfer_is_owner();
    }
    TaskHandle_t owner = xTaskGetCurrentTaskHandle();
    if (!owner || Platform_Flash_SetOperationCallback(filesystem_flash_irq, owner) != PLATFORM_OK)
    {
        return false;
    }
    filesystem_flash_owner = owner;
    filesystem_flash_notify_index = notify_index;
    filesystem_flash_registered = true;
    return true;
}

/**
 * @brief 将毫秒预算转换为至少一个 RTOS tick。
 * @param[in] ms 待转换毫秒数，零值也按一个 tick 处理。
 * @return 不小于 1 的 tick 数，用于通知等待和预算比较。
 */
static TickType_t filesystem_flash_ticks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks ? ticks : 1;
}

/**
 * @brief 将已受理的异步 Platform 请求推进为 Service 同步返回。
 * @param started 对应 Start 的立即结果；非 OK 时不推进硬件。
 * @param timeout_ms 触发 Abort 的预算，单位毫秒，不是强制返回的硬时限。
 * @return 成功、忙、错误或超时；仅允许初始化后的所有者任务调用。
 * @note 硬件阶段等待索引通知并周期重查，软件阶段按预算让出调度。
 *       超时后仍持续 Process，直到控制器/DMA 不再访问缓冲；若无法静止则保持等待，
 *       防止返回后 DMA 继续访问调用者已复用的内存。
 */
Service_StatusTypeDef filesystem_flash_transfer_finish(Platform_StatusTypeDef started,
                                                       uint32_t timeout_ms)
{
    if (!filesystem_flash_transfer_is_owner())
    {
        return SERVICE_NOT_READY;
    }
    if (started != PLATFORM_OK)
    {
        return started == PLATFORM_BUSY ? SERVICE_BUSY : SERVICE_ERROR;
    }
    TickType_t start_tick = xTaskGetTickCount();
    bool aborted = false;
    uint32_t software_steps = 0;
    for (;;)
    {
        Platform_StatusTypeDef status = Platform_Flash_ProcessOperation();
        if (status != PLATFORM_BUSY)
        {
            return aborted ? SERVICE_TIMEOUT : (status == PLATFORM_OK ? SERVICE_OK : SERVICE_ERROR);
        }
        if (!aborted &&
            (TickType_t)(xTaskGetTickCount() - start_tick) >= filesystem_flash_ticks(timeout_ms))
        {
            Platform_Flash_AbortOperation();
            aborted = true;
        }
        if (Platform_Flash_OperationNeedsWait())
        {
            uint32_t notification;
            /* 通知只是提示，结果只相信 Process。延迟/旧通知不能当成当前请求完成。 */
            (void)xTaskNotifyWaitIndexed((UBaseType_t)filesystem_flash_notify_index,
                                         0,
                                         UINT32_MAX,
                                         &notification,
                                         filesystem_flash_ticks(FILESYSTEM_FLASH_WAIT_SLICE_MS));
            software_steps = 0;
        }
        else if (++software_steps >= FILESYSTEM_FLASH_SOFTWARE_YIELD_STEPS)
        {
            vTaskDelay(1);
            software_steps = 0;
        }
        /* 超时后仍要安全收尾，不能因预算到期让 DMA 继续访问已返回的缓冲。 */
    }
}

/**
 * @brief 结合卷状态细分打开失败，保留已完成和超时结果。
 * @param[in] result 同步执行器完成打开后的 Service 结果。
 * @return OK/TIMEOUT 原样返回；无格式返回 NO_FILESYSTEM；RESET/INCOMPLETE/INCOMPATIBLE 返回 NOT_READY；
 *       其余返回 ERROR。
 * @note 只读取卷状态，不触发格式化或重试。
 */
static Service_StatusTypeDef filesystem_flash_open_status(Service_StatusTypeDef result)
{
    if (result == SERVICE_OK || result == SERVICE_TIMEOUT)
    {
        return result;
    }
    switch (Platform_Flash_GetVolumeState())
    {
        case PLATFORM_FLASH_VOLUME_UNFORMATTED:
            return SERVICE_NO_FILESYSTEM;
        case PLATFORM_FLASH_VOLUME_RESET:
        case PLATFORM_FLASH_VOLUME_INCOMPLETE:
        case PLATFORM_FLASH_VOLUME_INCOMPATIBLE:
            return SERVICE_NOT_READY;
        default:
            return SERVICE_ERROR;
    }
}

/**
 * @brief 绑定并同步扫描 Flash 卷，已就绪时直接成功。
 * @return SERVICE_OK 表示卷就绪；NOT_READY 表示所有者或绑定条件不满足；其余为打开结果归一化后的状态。
 * @note 仅 Storage Task 调用；不自动格式化。按宏可输出格式版本、代次、块统计及扫描耗时；扫描等待可让出 CPU。
 */
Service_StatusTypeDef filesystem_flash_transfer_open(void)
{
    if (!filesystem_flash_transfer_is_owner())
    {
        return SERVICE_NOT_READY;
    }
    if (Platform_Flash_GetVolumeState() == PLATFORM_FLASH_VOLUME_READY)
    {
        return SERVICE_OK;
    }
    if (Platform_Flash_BindVolume() != PLATFORM_OK)
    {
        return SERVICE_NOT_READY;
    }
#if FILESYSTEM_FLASH_DIAG_LOG_ENABLE
    TickType_t start_tick = xTaskGetTickCount();
#endif
    Service_StatusTypeDef result = filesystem_flash_open_status(filesystem_flash_transfer_finish(
        Platform_Flash_OpenVolumeStart(), FILESYSTEM_FLASH_OPEN_TIMEOUT_MS));
#if FILESYSTEM_FLASH_DIAG_LOG_ENABLE
    Platform_Flash_VolumeDiagnosticsTypeDef diagnostics;
    if (Platform_Flash_GetVolumeDiagnostics(&diagnostics) == PLATFORM_OK)
    {
        char text[160];
        (void)snprintf(text,
                       sizeof(text),
                       "FTL fmt=%lu epoch=%llu valid=%lu free=%lu stale=%lu scan=%lu ms",
                       (unsigned long)diagnostics.FormatVersion,
                       (unsigned long long)diagnostics.Epoch,
                       (unsigned long)diagnostics.ValidGroups,
                       (unsigned long)diagnostics.FreeBlocks,
                       (unsigned long)diagnostics.StaleBlocks,
                       (unsigned long)((xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS));
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, "FLASH", text);
    }
#endif
    return result;
}

/**
 * @brief 同步执行显式 FTL 格式化，不创建 FAT 文件系统。
 * @return SERVICE_OK 表示格式化完成；NOT_READY 表示所有者或绑定无效；其余为启动、执行或超时失败。
 * @note 破坏绑定分区内数据；上层须先注销文件系统并确认允许格式化。超时后仍等待安全收尾。
 */
Service_StatusTypeDef filesystem_flash_transfer_format(void)
{
    if (!filesystem_flash_transfer_is_owner())
    {
        return SERVICE_NOT_READY;
    }
    if (Platform_Flash_BindVolume() != PLATFORM_OK)
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_FormatVolumeStart(),
                                            FILESYSTEM_FLASH_FORMAT_TIMEOUT_MS);
}

/**
 * @brief 在唯一任务中同步读取逻辑扇区。
 * @param[in] lba 起始逻辑扇区号。
 * @param[out] data 至少 count * 512 B 的调用者缓冲，返回前保持有效。
 * @param[in] count 非零逻辑扇区数，完整范围不得越卷容量。
 * @return SERVICE_OK 表示请求完成；NOT_READY 表示非所有者；BUSY/ERROR/TIMEOUT 表示启动或执行失败。
 * @note 返回成功才可使用完整输出；失败可能已更新部分数据。 DMA 使用 FTL 内部缓冲；任务可等待让出 CPU，超时仍须安全收尾。
 */
Service_StatusTypeDef filesystem_flash_transfer_read(uint32_t lba, uint8_t *data, uint32_t count)
{
    if (!filesystem_flash_transfer_is_owner())
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_ReadBlocksStart(lba, data, count),
                                            FILESYSTEM_FLASH_READ_TIMEOUT_MS);
}

/**
 * @brief 在唯一任务中同步写入逻辑扇区并等待提交完成。
 * @param[in] lba 起始逻辑扇区号。
 * @param[in] data 至少 count * 512 B 的调用者缓冲，返回前保持有效。
 * @param[in] count 非零逻辑扇区数，完整范围不得越卷容量。
 * @return SERVICE_OK 表示请求完成；NOT_READY 表示非所有者；BUSY/ERROR/TIMEOUT 表示启动或执行失败。
 * @note 返回前输入不可改写；跨组失败可能已提交部分组，不承诺整笔请求原子性。 DMA 使用 FTL 内部缓冲；任务可等待让出 CPU，超时仍须安全收尾。
 */
Service_StatusTypeDef filesystem_flash_transfer_write(uint32_t lba,
                                                      const uint8_t *data,
                                                      uint32_t count)
{
    if (!filesystem_flash_transfer_is_owner())
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_WriteBlocksStart(lba, data, count),
                                            FILESYSTEM_FLASH_WRITE_TIMEOUT_MS);
}

/**
 * @brief 同步确认卷请求已完成，供 CTRL_SYNC 使用。
 * @return SERVICE_OK 表示同步完成；NOT_READY 表示非所有者；其余为启动、执行或超时失败。
 * @note 不执行 FatFs 的文件元数据刷新；本层没有只写 RAM 即成功的写回缓存，任务等待由执行器处理。
 */
Service_StatusTypeDef filesystem_flash_transfer_sync(void)
{
    if (!filesystem_flash_transfer_is_owner())
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_SyncVolumeStart(),
                                            FILESYSTEM_FLASH_WRITE_TIMEOUT_MS);
}

/**
 * @brief 在卷就绪且调用者持有执行权时同步执行一次 GC 回收。
 * @return SERVICE_OK 表示本次回收完成（也可能无须擦除）；NOT_READY 表示上下文或卷未就绪；其余为执行失败。
 * @note 由 Storage Task 空闲机会调用；FTL 决定水位和目标，一次最多回收一个块，擦除期间可能等待。
 */
Service_StatusTypeDef filesystem_flash_transfer_reclaim(void)
{
    if (!filesystem_flash_transfer_is_owner() ||
        Platform_Flash_GetVolumeState() != PLATFORM_FLASH_VOLUME_READY)
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_ReclaimVolumeStart(),
                                            FILESYSTEM_FLASH_RECLAIM_TIMEOUT_MS);
}

/**
 * @brief 等待 Platform 恢复原始器件，再强制重扫 FTL。
 * @return SERVICE_OK 表示恢复及重扫完成；NOT_READY 表示非所有者或卷不可用；TIMEOUT/ERROR 表示恢复失败，未格式化为 NO_FILESYSTEM。
 * @note 仅显式恢复流程调用，上层须先注销旧文件对象。恢复阶段每 tick 重试，成功后不沿用旧 RAM 映射；不自动挂载或格式化。
 */
Service_StatusTypeDef filesystem_flash_transfer_recover(void)
{
    if (!filesystem_flash_transfer_is_owner())
    {
        return SERVICE_NOT_READY;
    }
    TickType_t start = xTaskGetTickCount();
    for (;;)
    {
        Platform_StatusTypeDef status = Platform_Flash_RecoverVolume();
        if (status == PLATFORM_OK)
        {
            break;
        }
        if (status != PLATFORM_BUSY)
        {
            return SERVICE_ERROR;
        }
        if ((TickType_t)(xTaskGetTickCount() - start) >=
            filesystem_flash_ticks(FILESYSTEM_FLASH_RECOVERY_TIMEOUT_MS))
        {
            return SERVICE_TIMEOUT;
        }
        vTaskDelay(1);
    }
    /* 显式重扫，即使先前 RAM 映射曾处于 READY，也不能沿用。 */
    return filesystem_flash_open_status(filesystem_flash_transfer_finish(
        Platform_Flash_OpenVolumeStart(), FILESYSTEM_FLASH_OPEN_TIMEOUT_MS));
}
