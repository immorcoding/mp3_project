/**
  ******************************************************************************
  * @file    storage_flash.c
  * @brief   Storage Task 的 Platform Flash 异步传输协调实现。
  *
  * @details
  *          本 Module 是 QSPI IRQ 与 Storage Task 普通上下文之间的 APP 层
  *          协调点。IRQ 仅写入索引任务通知；任务醒来后才调用 Platform Flash
  *          收尾，使 Adapter 的 D-Cache 维护与 W25Qxx 状态变更均不发生在
  *          中断上下文。当前协调 0xEC 的 QSPI/MDMA 数组读取；后续自动状态
  *          轮询的 Status Match 也应接入同一唯一订阅者，而不能由各业务模块
  *          临时抢占 Platform Flash 回调槽。
  ******************************************************************************
  */

#include "APP/tasks/storage/storage_flash.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/log/log_service.h"

/** @brief 本 Module 发送初始化失败日志时使用的稳定标签。 */
static const char storage_flash_log_tag[] = "FLASH";

/** @brief 唯一允许提交或等待 Flash 异步传输的 Storage Task。 */
static TaskHandle_t storage_flash_task_handle;

/** @brief Platform Flash 的唯一 IRQ 订阅是否已经由本 Module 成功建立。 */
static bool storage_flash_callback_registered;

/**
  * @brief  在 QSPI IRQ 中唤醒当前等待 Flash 异步传输的 Storage Task。
  * @param  event Platform Flash 归类后的完成、错误或中止事件。
  * @param  context 注册时注入的 Storage TaskHandle_t。
  * @note   回调不解释 event；Platform_Flash_ProcessTransfer() 会在普通上下文
  *         查询 Adapter 的最终状态。这里不得访问 DMA 缓冲区、记录日志或启动
  *         下一笔 QSPI 操作。
  */
static void storage_flash_transfer_callback(
    Platform_Flash_TransferEventTypeDef event,
    void *context)
{
    TaskHandle_t task_handle = (TaskHandle_t)context;
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)event;

    if (task_handle != NULL)
    {
        vTaskNotifyGiveIndexedFromISR(
            task_handle,
            FREERTOS_NOTIFY_INDEX_STORAGE_FLASH_TRANSFER,
            &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/**
  * @brief  确认当前调用者拥有 Flash 协调 Module，并清除上一笔残留通知。
  * @retval true 可安全启动下一笔由本 Module 等待的 Platform Flash 传输。
  * @retval false Module 未初始化，或调用者不是 Storage Task。
  * @note   必须在启动异步 QSPI 操作前调用。若 IRQ 在启动函数返回前到达，计数型
  *         任务通知仍会保留完成事件，后续等待会立即返回。
  */
static bool storage_flash_prepare_transfer(void)
{
    if ((!storage_flash_callback_registered) ||
        (storage_flash_task_handle == NULL) ||
        (xTaskGetCurrentTaskHandle() != storage_flash_task_handle))
    {
        return false;
    }

    (void)ulTaskNotifyTakeIndexed(
        FREERTOS_NOTIFY_INDEX_STORAGE_FLASH_TRANSFER,
        pdTRUE,
        0U);
    return true;
}

/**
  * @brief  绑定当前 Storage Task 为 Platform Flash 的长期 IRQ 订阅者。
  * @param  task_handle 当前 Storage Task 的有效任务句柄。
  * @note   初始化成功后，其他 APP Module 不得调用
  *         Platform_Flash_SetTransferCallback() 或 ClearTransferCallback()；
  *         所有 QSPI 异步传输均须经本 Module 等待并收尾。
  */
void storage_flash_init(TaskHandle_t task_handle)
{
    if (task_handle == NULL)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_log_tag,
                               "Storage task handle is invalid.");
        return;
    }

    if (storage_flash_callback_registered)
    {
        if (storage_flash_task_handle != task_handle)
        {
            (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                                   storage_flash_log_tag,
                                   "Transfer owner changed unexpectedly.");
        }

        return;
    }

    if (Platform_Flash_SetTransferCallback(storage_flash_transfer_callback,
                                           task_handle) != PLATFORM_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_log_tag,
                               "Transfer callback setup failed.");
        return;
    }

    storage_flash_task_handle = task_handle;
    storage_flash_callback_registered = true;
}

/**
 * @brief  等待一笔已由本 Module 启动的 QSPI/MDMA 读取并完成普通上下文收尾。
 * @param  timeout_ms 等待 QSPI IRQ 的最大时长，必须非零。
 * @retval true 已收到完成通知，且 Platform 收尾成功；调用者随后可访问 data。
 * @retval false 等待超时、IRQ 报错/中止，或 Component/Adapter/Cache 收尾失败。
 * @note   超时后额外让出一个 Tick，使 HAL Tick 先超过 Component 数组读取时限，
 *         再由 Platform_Flash_ProcessTransfer() 统一记录失败状态。该规则只服务
 *         当前 MDMA 读取；后续 WIP 自动轮询将以其独立的操作截止时间收尾。
 */
static bool storage_flash_finish_transfer(uint32_t timeout_ms)
{
    if ((storage_flash_task_handle == NULL) ||
        (xTaskGetCurrentTaskHandle() != storage_flash_task_handle) ||
        (timeout_ms == 0U))
    {
        return false;
    }

    if (ulTaskNotifyTakeIndexed(
            FREERTOS_NOTIFY_INDEX_STORAGE_FLASH_TRANSFER,
            pdTRUE,
            pdMS_TO_TICKS(timeout_ms)) == 0U)
    {
        /* 任务 Tick 与 HAL Tick 可能相差一个边界 Tick，先让 Component 超时成立。 */
        vTaskDelay(1U);
        (void)Platform_Flash_ProcessTransfer();
        return false;
    }

    return Platform_Flash_ProcessTransfer() == PLATFORM_OK;
}

/**
  * @brief  读取一次 0xEC 物理数组，并由 Storage Task 完成 MDMA 等待与收尾。
  * @param  address 待读取物理数组的 4-byte 对齐首地址。
  * @param  data 接收数据的 MDMA 可访问且严格 Cache-line 对齐的缓冲区。
  * @param  data_length 接收长度，必须完整落在 Flash 数组范围内。
  * @param  timeout_ms 等待当前读取完成的最大时长，必须非零。
  * @retval true DMA 已完成普通上下文收尾，data 已可由 CPU 读取。
  * @retval false 调用上下文、订阅、参数、读取启动、IRQ 等待或收尾失败。
  */
bool storage_flash_read_array(uint32_t address,
                              uint8_t *data,
                              uint32_t data_length,
                              uint32_t timeout_ms)
{
    if (!storage_flash_prepare_transfer())
    {
        return false;
    }

    if (Platform_Flash_StartReadArray(address, data, data_length) != PLATFORM_OK)
    {
        return false;
    }

    return storage_flash_finish_transfer(timeout_ms);
}

/**
  * @brief  读取一次受限自检扇区，并由 Storage Task 完成 MDMA 等待与收尾。
  * @param  region Platform 定义的首或尾自检扇区语义。
  * @param  data 接收数据的 MDMA 可访问且严格 Cache-line 对齐的缓冲区。
  * @param  data_length 接收长度，当前必须为一个 4 KiB 自检扇区。
  * @param  timeout_ms 等待当前读取完成的最大时长，必须非零。
  * @retval true DMA 已完成普通上下文收尾，data 已可由 CPU 读取。
  * @retval false 调用上下文、订阅、区域、参数、读取启动、IRQ 等待或收尾失败。
  */
bool storage_flash_read_diagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms)
{
    if (!storage_flash_prepare_transfer())
    {
        return false;
    }

    if (Platform_Flash_StartDiagnosticRead(region, data, data_length) != PLATFORM_OK)
    {
        return false;
    }

    return storage_flash_finish_transfer(timeout_ms);
}
