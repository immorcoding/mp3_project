/**
  ******************************************************************************
  * @file    storage_flash.c
 * @brief   Storage Task 的 Platform Flash 异步操作协调实现。
  *
  * @details
  *          本 Module 是 QSPI IRQ 与 Storage Task 普通上下文之间的 APP 层
  *          协调点。IRQ 仅写入索引任务通知；任务醒来后才调用 Platform Flash
 *          收尾，使 Adapter 的 D-Cache 维护与 W25Qxx 状态变更均不发生在
 *          中断上下文。当前同时协调 0xEC 的 QSPI/MDMA 数组读取，以及 0x34
 *          页编程、0x21 扇区擦除后的 WIP 自动轮询；任何业务模块均不得临时抢占
 *          Platform Flash 回调槽。
  ******************************************************************************
  */

#include "APP/tasks/storage/storage_flash.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Service/log/log_service.h"

/** @brief 本 Module 发送初始化失败日志时使用的稳定标签。 */
static const char storage_flash_log_tag[] = "FLASH";

/** @brief 唯一允许提交或等待 Flash 异步操作的 Storage Task。 */
static TaskHandle_t storage_flash_task_handle;

/** @brief Platform Flash 的唯一 IRQ 订阅是否已经由本 Module 成功建立。 */
static bool storage_flash_callback_registered;

/**
  * @brief  在 QSPI IRQ 中唤醒当前等待 Flash 异步操作的 Storage Task。
  * @param  event Platform Flash 归类后的读完成、状态匹配、错误或中止事件。
  * @param  context 注册时注入的 Storage TaskHandle_t。
  * @note   回调只把 event 作为通知值保存；Platform_Flash_ProcessOperation() 会
  *         在普通上下文查询 Adapter 的最终状态。这里不得访问 DMA 缓冲区、记录
  *         日志或启动下一笔 QSPI 操作。
  */
static void storage_flash_operation_callback(
    Platform_Flash_OperationEventTypeDef event,
    void *context)
{
    TaskHandle_t task_handle = (TaskHandle_t)context;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (task_handle != NULL)
    {
        (void)xTaskNotifyIndexedFromISR(
            task_handle,
            FREERTOS_NOTIFY_INDEX_STORAGE_FLASH_OPERATION,
            (uint32_t)event,
            eSetValueWithOverwrite,
            &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/**
  * @brief  确认当前调用者拥有 Flash 协调 Module，并清除上一笔残留通知。
  * @retval true 可安全启动下一笔由本 Module 等待的 Platform Flash 操作。
  * @retval false Module 未初始化，或调用者不是 Storage Task。
  * @note   必须在启动异步 QSPI 操作前调用。若 IRQ 在启动函数返回前到达，覆盖式
  *         值通知仍会保留最终事件值，后续等待会立即返回。
  */
static bool storage_flash_prepare_operation(void)
{
    if ((!storage_flash_callback_registered) ||
        (storage_flash_task_handle == NULL) ||
        (xTaskGetCurrentTaskHandle() != storage_flash_task_handle))
    {
        return false;
    }

    (void)xTaskNotifyStateClearIndexed(
        storage_flash_task_handle,
        FREERTOS_NOTIFY_INDEX_STORAGE_FLASH_OPERATION);
    return true;
}

/**
  * @brief  绑定当前 Storage Task 为 Platform Flash 的长期 IRQ 订阅者。
  * @param  task_handle 当前 Storage Task 的有效任务句柄。
  * @note   初始化成功后，其他 APP Module 不得调用
  *         Platform_Flash_SetOperationCallback() 或 ClearOperationCallback()；
  *         所有 QSPI 异步操作均须经本 Module 等待并收尾。
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
                                   "Operation owner changed unexpectedly.");
        }

        return;
    }

    if (Platform_Flash_SetOperationCallback(storage_flash_operation_callback,
                                            task_handle) != PLATFORM_OK)
    {
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_ERROR,
                               storage_flash_log_tag,
                               "Operation callback setup failed.");
        return;
    }

    storage_flash_task_handle = task_handle;
    storage_flash_callback_registered = true;
}

/**
 * @brief  等待一笔已由本 Module 启动的 QSPI 异步操作并完成普通上下文收尾。
 * @param  expected_event 当前操作预期的最终 QSPI IRQ 事件。
 * @param  timeout_ms 等待 QSPI IRQ 的最大时长，必须非零。
 * @retval true 已收到预期完成通知，且 Platform 收尾成功。
 * @retval false 等待超时、IRQ 报错/中止，或 Component/Adapter/Cache 收尾失败。
 * @note   超时后额外让出一个 Tick，使 HAL Tick 先超过 Component 的软件时限，
 *         再由 Platform_Flash_ProcessOperation() 统一中止 QSPI 自动轮询并记录
 *         失败。读取预期 READ_COMPLETE，页编程和擦除预期 STATUS_MATCH。
 */
static bool storage_flash_finish_operation(
    Platform_Flash_OperationEventTypeDef expected_event,
    uint32_t timeout_ms)
{
    uint32_t event_value;
    if ((storage_flash_task_handle == NULL) ||
        (xTaskGetCurrentTaskHandle() != storage_flash_task_handle) ||
        (timeout_ms == 0U))
    {
        return false;
    }

    if (xTaskNotifyWaitIndexed(
            FREERTOS_NOTIFY_INDEX_STORAGE_FLASH_OPERATION,
            0u,
            UINT32_MAX,
            &event_value,
            pdMS_TO_TICKS(timeout_ms)) != pdPASS)
    {
        /* 任务 Tick 与 HAL Tick 可能相差一个边界 Tick，先让 Component 超时成立。 */
        vTaskDelay(1U);
        (void)Platform_Flash_ProcessOperation();
        return false;
    }

    if (event_value != (uint32_t)expected_event)
    {
        (void)Platform_Flash_ProcessOperation();
        return false;
    }

    return Platform_Flash_ProcessOperation() == PLATFORM_OK;
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
    if (!storage_flash_prepare_operation())
    {
        return false;
    }

    if (Platform_Flash_StartReadArray(address, data, data_length) != PLATFORM_OK)
    {
        return false;
    }

    return storage_flash_finish_operation(
        PLATFORM_FLASH_OPERATION_EVENT_READ_COMPLETE,
        timeout_ms);
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
    if (!storage_flash_prepare_operation())
    {
        return false;
    }

    if (Platform_Flash_StartDiagnosticRead(region, data, data_length) != PLATFORM_OK)
    {
        return false;
    }

    return storage_flash_finish_operation(
        PLATFORM_FLASH_OPERATION_EVENT_READ_COMPLETE,
        timeout_ms);
}

/**
 * @brief  擦除一个 ADR-0009 保留自检扇区并等待 WIP 自动轮询完成。
 * @param  region Platform 定义的首或尾自检扇区语义。
 * @param  timeout_ms 等待 STATUS_MATCH 的最大时长，必须非零。
 * @retval true 4 KiB 擦除已经完成，W25Qxx Device 已回到 READY。
 * @retval false 调用上下文、订阅、区域、启动、IRQ 等待或收尾失败。
 */
bool storage_flash_erase_diagnostic(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t timeout_ms)
{
    if (!storage_flash_prepare_operation())
    {
        return false;
    }

    if (Platform_Flash_StartDiagnosticErase(region) != PLATFORM_OK)
    {
        return false;
    }

    return storage_flash_finish_operation(
        PLATFORM_FLASH_OPERATION_EVENT_STATUS_MATCH,
        timeout_ms);
}

/**
 * @brief  编程一个 ADR-0009 保留自检扇区内的物理页并等待 WIP 自动轮询完成。
 * @param  region Platform 定义的首或尾自检扇区语义。
 * @param  page_offset 相对扇区页首的 256-byte 对齐偏移。
 * @param  data 当前页的有效数据。
 * @param  data_length 当前页写入长度，范围 1..256 且不得跨页。
 * @param  timeout_ms 等待 STATUS_MATCH 的最大时长，必须非零。
 * @retval true 页编程已经完成，W25Qxx Device 已回到 READY。
 * @retval false 调用上下文、订阅、参数、启动、IRQ 等待或收尾失败。
 */
bool storage_flash_program_diagnostic_page(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t page_offset,
    const uint8_t *data,
    uint32_t data_length,
    uint32_t timeout_ms)
{
    if (!storage_flash_prepare_operation())
    {
        return false;
    }

    if (Platform_Flash_StartDiagnosticPageProgram(region,
                                                   page_offset,
                                                   data,
                                                   data_length) != PLATFORM_OK)
    {
        return false;
    }

    return storage_flash_finish_operation(
        PLATFORM_FLASH_OPERATION_EVENT_STATUS_MATCH,
        timeout_ms);
}
