/**
 * @file filesystem_flash_transfer.c
 * @brief Flash 同步执行、完成信号量等待和安全收尾；调用任务不固定。
 */

#include "Service/filesystem/flash/filesystem_flash_transfer.h"
#include "Service/filesystem/flash/filesystem_flash_config.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/log/log_service.h"
#include <stdio.h>

/** @brief 完成信号量的静态存储；句柄在第一次成功注册时建立，之后不再重建。 */
static StaticSemaphore_t filesystem_flash_done_storage;
static SemaphoreHandle_t filesystem_flash_done;

/**
 * @brief 发布轻量唤醒提示，不在 ISR 中推进 Flash 请求。
 * @param event Platform 已归一化的事件，只作为任务重查提示，这里不用。
 * @param context 注册时交给 Platform 的完成信号量句柄。
 * @note 运行在 ISR 上下文，只用 FromISR 版本，并把“唤醒了更高优先级任务”交给
 *       portYIELD_FROM_ISR。信号可合并或延迟，真正完成由等待任务调用 Process 判定；
 *       不访问数据缓冲。
 */
static void filesystem_flash_irq(Platform_Flash_OperationEventTypeDef event, void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)event;
    if (context == NULL)
    {
        return;
    }
    (void)xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief 判断 Flash 执行器是否已经初始化（完成信号量已建立且事件回调已注册）。
 * @return true 表示已调用过成功的 filesystem_flash_transfer_init()。
 * @note 只回答“能不能等”，不看调用任务是谁，不做锁仲裁，也不推进硬件。
 */
bool filesystem_flash_transfer_is_ready(void)
{
    return filesystem_flash_done != NULL;
}

/**
 * @brief 建立完成信号量并向 Platform 注册长期事件回调。
 * @return true 表示首次注册成功或已经注册过；false 表示信号量创建或 Platform 订阅失败。
 * @note 信号量先于订阅创建：Platform 只在订阅之后才会调用 ISR 回调，所以 ISR 看到的
 *       信号量总是已经存在的。重复调用不重建信号量，也不重新订阅。调用者（`InitFlash` 与挂载路径）
 *       已持 Flash 卷锁，所以第一次调用与别的调用串行，不会重复创建。注册成功后不再临时转移回调。
 */
bool filesystem_flash_transfer_init(void)
{
    SemaphoreHandle_t done;

    if (filesystem_flash_done != NULL)
    {
        return true;
    }
    done = xSemaphoreCreateBinaryStatic(&filesystem_flash_done_storage);
    if ((done == NULL) ||
        (Platform_Flash_SetOperationCallback(filesystem_flash_irq, (void *)done) != PLATFORM_OK))
    {
        return false;
    }
    filesystem_flash_done = done;
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
 * @return 成功、忙、错误或超时；初始化之后任何任务都可以调用。
 * @note 硬件阶段等待完成信号量并周期重查，软件阶段按预算让出调度。
 *       超时后仍持续 Process，直到控制器/DMA 不再访问缓冲；若无法静止则保持等待，
 *       防止返回后 DMA 继续访问调用者已复用的内存。
 *       进入等待前先丢掉遗留信号（上一次操作的最后一个中断可能没被取走）：Start 已经
 *       发生，所以清完之后第一次 Process 才是权威；清理之后到达的信号属于本次操作，保留。
 *       遗留信号最坏让第一次等待提前醒来一次，Process 仍报 BUSY 就再等，所以无害，清掉
 *       只是让每次等待的含义不依赖上一次操作。
 */
Service_StatusTypeDef filesystem_flash_transfer_finish(Platform_StatusTypeDef started,
                                                       uint32_t timeout_ms)
{
    if (!filesystem_flash_transfer_is_ready())
    {
        return SERVICE_NOT_READY;
    }
    if (started != PLATFORM_OK)
    {
        return started == PLATFORM_BUSY ? SERVICE_BUSY : SERVICE_ERROR;
    }
    (void)xSemaphoreTake(filesystem_flash_done, 0U);
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
            /* 信号只是提示，结果只相信 Process。延迟/旧信号不能当成当前请求完成。 */
            (void)xSemaphoreTake(filesystem_flash_done,
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
 * @return SERVICE_OK 表示卷就绪；NOT_READY 表示执行器未初始化或绑定条件不满足；其余为打开结果归一化后的状态。
 * @note 不自动格式化。按宏可输出格式版本、代次、块统计及扫描耗时；扫描等待可让出 CPU。
 */
Service_StatusTypeDef filesystem_flash_transfer_open(void)
{
    if (!filesystem_flash_transfer_is_ready())
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
 * @return SERVICE_OK 表示格式化完成；NOT_READY 表示执行器未初始化或绑定无效；其余为启动、执行或超时失败。
 * @note 破坏绑定分区内数据；上层须先注销文件系统并确认允许格式化。超时后仍等待安全收尾。
 */
Service_StatusTypeDef filesystem_flash_transfer_format(void)
{
    if (!filesystem_flash_transfer_is_ready())
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
 * @brief 同步读取逻辑扇区。
 * @param[in] lba 起始逻辑扇区号。
 * @param[out] data 至少 count * 512 B 的调用者缓冲，返回前保持有效。
 * @param[in] count 非零逻辑扇区数，完整范围不得越卷容量。
 * @return SERVICE_OK 表示请求完成；NOT_READY 表示执行器未初始化；BUSY/ERROR/TIMEOUT 表示启动或执行失败。
 * @note 返回成功才可使用完整输出；失败可能已更新部分数据。 DMA 使用 FTL 内部缓冲；任务可等待让出 CPU，超时仍须安全收尾。
 */
Service_StatusTypeDef filesystem_flash_transfer_read(uint32_t lba, uint8_t *data, uint32_t count)
{
    if (!filesystem_flash_transfer_is_ready())
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_ReadBlocksStart(lba, data, count),
                                            FILESYSTEM_FLASH_READ_TIMEOUT_MS);
}

/**
 * @brief 同步写入逻辑扇区并等待提交完成。
 * @param[in] lba 起始逻辑扇区号。
 * @param[in] data 至少 count * 512 B 的调用者缓冲，返回前保持有效。
 * @param[in] count 非零逻辑扇区数，完整范围不得越卷容量。
 * @return SERVICE_OK 表示请求完成；NOT_READY 表示执行器未初始化；BUSY/ERROR/TIMEOUT 表示启动或执行失败。
 * @note 返回前输入不可改写；跨组失败可能已提交部分组，不承诺整笔请求原子性。 DMA 使用 FTL 内部缓冲；任务可等待让出 CPU，超时仍须安全收尾。
 */
Service_StatusTypeDef filesystem_flash_transfer_write(uint32_t lba,
                                                      const uint8_t *data,
                                                      uint32_t count)
{
    if (!filesystem_flash_transfer_is_ready())
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_WriteBlocksStart(lba, data, count),
                                            FILESYSTEM_FLASH_WRITE_TIMEOUT_MS);
}

/**
 * @brief 同步确认卷请求已完成，供 CTRL_SYNC 使用。
 * @return SERVICE_OK 表示同步完成；NOT_READY 表示执行器未初始化；其余为启动、执行或超时失败。
 * @note 不执行 FatFs 的文件元数据刷新；本层没有只写 RAM 即成功的写回缓存，任务等待由执行器处理。
 */
Service_StatusTypeDef filesystem_flash_transfer_sync(void)
{
    if (!filesystem_flash_transfer_is_ready())
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_SyncVolumeStart(),
                                            FILESYSTEM_FLASH_WRITE_TIMEOUT_MS);
}

/**
 * @brief 在卷就绪时同步执行一次 GC 回收。
 * @return SERVICE_OK 表示本次回收完成（也可能无须擦除）；NOT_READY 表示执行器未初始化或卷未就绪；其余为执行失败。
 * @note 由 APP 在空闲机会调用；FTL 决定水位和目标，一次最多回收一个块，擦除期间可能等待。
 */
Service_StatusTypeDef filesystem_flash_transfer_reclaim(void)
{
    if (!filesystem_flash_transfer_is_ready() ||
        Platform_Flash_GetVolumeState() != PLATFORM_FLASH_VOLUME_READY)
    {
        return SERVICE_NOT_READY;
    }
    return filesystem_flash_transfer_finish(Platform_Flash_ReclaimVolumeStart(),
                                            FILESYSTEM_FLASH_RECLAIM_TIMEOUT_MS);
}

/**
 * @brief 等待 Platform 恢复原始器件，再强制重扫 FTL。
 * @return SERVICE_OK 表示恢复及重扫完成；NOT_READY 表示执行器未初始化或卷不可用；TIMEOUT/ERROR 表示恢复失败，未格式化为 NO_FILESYSTEM。
 * @note 仅显式恢复流程调用，上层须先注销旧文件对象。恢复阶段每 tick 重试，成功后不沿用旧 RAM 映射；不自动挂载或格式化。
 */
Service_StatusTypeDef filesystem_flash_transfer_recover(void)
{
    if (!filesystem_flash_transfer_is_ready())
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
