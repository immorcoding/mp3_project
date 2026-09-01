/**
  ******************************************************************************
  * @file    filesystem_sd_transfer.c
  * @brief   在异步 SDMMC DMA 之上执行同步 FatFs 块读写。
  *
  * @details
 *          本 Module 由 Filesystem Service 持有，只能在 Storage Task 中进入。
 *          它把 Storage Task 句柄、索引通知、DMA 可访问的 bounce buffer、分块和
 *          Platform SD 状态提交隐藏在同步块读写 Interface 后。Cortex-M7 D-Cache
 *          维护属于 SD STM32 HAL Adapter 的后端 Implementation。
  ******************************************************************************
  */

#include "Service/filesystem/sd/filesystem_sd_transfer.h"
#include "Service/filesystem/filesystem_config.h"

#include <limits.h>
#include <string.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Platform/sd/platform_sd.h"

#if ((FILESYSTEM_SD_BLOCK_SIZE % FILESYSTEM_SD_DMA_BUFFER_ALIGNMENT) != 0U)
#error "SD logical block size must satisfy the SD DMA buffer alignment contract."
#endif

static TaskHandle_t filesystem_sd_transfer_owner_task;

static uint8_t filesystem_sd_dma_buffer[
    FILESYSTEM_SD_BLOCK_SIZE * FILESYSTEM_SD_DMA_BLOCK_COUNT]
    __attribute__((aligned(FILESYSTEM_SD_DMA_BUFFER_ALIGNMENT), section(".sd_dma_buffer")));

/**
  * @brief  向 Storage Task 发布一个 SDMMC 传输事件。
  * @param  event 在 ISR 上下文收到的 Platform 归一化传输结果。
  * @param  context 向 Platform SD 注册的内部传输 Module 上下文。
  * @note   此回调有意不访问 DMA buffer、FatFs 或 SDCard 状态机；等待中的任务离开
  *         ISR 上下文后再执行这些操作。
  */
static void filesystem_sd_transfer_irq_callback(
    Platform_SD_TransferEventTypeDef event,
    void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if ((context != &filesystem_sd_transfer_owner_task) ||
        (filesystem_sd_transfer_owner_task == NULL))
    {
        return;
    }

    (void)xTaskNotifyIndexedFromISR(filesystem_sd_transfer_owner_task,
                                    FREERTOS_NOTIFY_INDEX_STORAGE_SD_TRANSFER,
                                    (uint32_t)event,
                                    eSetValueWithOverwrite,
                                    &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
  * @brief  校验调用者并清除残留的 DMA 事件通知。
  * @retval true 当前任务持有传输 Module，可启动一次 DMA。
  * @retval false 调用不在 Storage Task 上下文中。
  */
static bool filesystem_sd_transfer_prepare_wait(void)
{
    if ((filesystem_sd_transfer_owner_task == NULL) ||
        (xTaskGetCurrentTaskHandle() != filesystem_sd_transfer_owner_task))
    {
        return false;
    }

    (void)xTaskNotifyStateClearIndexed(
        filesystem_sd_transfer_owner_task,
        FREERTOS_NOTIFY_INDEX_STORAGE_SD_TRANSFER);
    return true;
}

/**
  * @brief  等待恰好一个 Platform SDMMC 传输事件。
  * @param  event 接收 ISR 发布的事件。
  * @param  timeout_ms 本 DMA 分块的最长等待时间，单位为毫秒。
  * @retval true 已收到完整传输事件。
  * @retval false 调用者或输出参数无效，或等待超时。
  */
static bool filesystem_sd_transfer_wait(Platform_SD_TransferEventTypeDef *event,
                                        uint32_t timeout_ms)
{
    uint32_t notification_value;

    if ((event == NULL) ||
        (filesystem_sd_transfer_owner_task == NULL) ||
        (xTaskGetCurrentTaskHandle() != filesystem_sd_transfer_owner_task))
    {
        return false;
    }

    if (xTaskNotifyWaitIndexed(FREERTOS_NOTIFY_INDEX_STORAGE_SD_TRANSFER,
                               0U,
                               UINT32_MAX,
                               &notification_value,
                               pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return false;
    }

    *event = (Platform_SD_TransferEventTypeDef)notification_value;
    return true;
}

/**
  * @brief  将收到的 DMA 事件提交给 Platform SD 状态机。
  * @param  expected_event 当前操作方向所要求的完成事件。
  * @param  timeout_ms 本 DMA 分块的最长等待时间，单位为毫秒。
  * @retval true DMA 与传输后的 SD 卡同步均成功。
  * @retval false 超时、错误、中止、方向不符或同步失败。
  */
static bool filesystem_sd_transfer_finish(
    Platform_SD_TransferEventTypeDef expected_event,
    uint32_t timeout_ms)
{
    Platform_SD_TransferEventTypeDef event;

    if (!filesystem_sd_transfer_wait(&event, timeout_ms))
    {
        (void)Platform_SD_CompleteTransfer(PLATFORM_SD_TRANSFER_EVENT_ERROR);
        return false;
    }

    if (event != expected_event)
    {
        (void)Platform_SD_CompleteTransfer(event);
        return false;
    }

    return Platform_SD_CompleteTransfer(event) == PLATFORM_OK;
}

/**
  * @brief  将 Filesystem Service DMA 执行器绑定到唯一的 Storage Task。
  * @retval true 当前任务持有执行器并接收 DMA 事件。
  * @retval false 调度器未运行、已由其他任务持有，或 Platform SD 尚不能接收订阅者。
  * @note   允许同一 Storage Task 重复初始化：挂载和卸载路径也会把
  *         Service_Filesystem_InitSD() 用作幂等的就绪检查。
  */
bool filesystem_sd_transfer_init(void)
{
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();

    if (current_task == NULL)
    {
        return false;
    }

    if (filesystem_sd_transfer_owner_task == NULL)
    {
        filesystem_sd_transfer_owner_task = current_task;
    }

    if (filesystem_sd_transfer_owner_task != current_task)
    {
        return false;
    }

    return Platform_SD_SetTransferCallback(filesystem_sd_transfer_irq_callback,
                                           &filesystem_sd_transfer_owner_task) == PLATFORM_OK;
}

/**
  * @brief  经 Cache 安全的 DMA bounce buffer 读取一个或多个逻辑 SD 块。
  * @param  destination 接收请求块的调用者缓冲区。
  * @param  start_block 第一个待读取的逻辑块。
  * @param  block_count 连续读取的逻辑块数量。
  * @param  timeout_ms 每个 DMA 分块的最长等待时间，单位为毫秒。
  * @retval true DMA 成功后，所有块均已复制到 destination。
  * @retval false 输入、所有权、DMA 启动、完成或卡同步失败。
  */
bool filesystem_sd_transfer_read_blocks(uint8_t *destination,
                                        uint32_t start_block,
                                        uint32_t block_count,
                                        uint32_t timeout_ms)
{
    uint32_t remaining_blocks = block_count;
    uint32_t current_block = start_block;

    if ((destination == NULL) || (block_count == 0U))
    {
        return false;
    }

    while (remaining_blocks != 0U)
    {
        uint32_t chunk_blocks = remaining_blocks;
        uint32_t byte_count;

        if (chunk_blocks > FILESYSTEM_SD_DMA_BLOCK_COUNT)
        {
            chunk_blocks = FILESYSTEM_SD_DMA_BLOCK_COUNT;
        }

        byte_count = chunk_blocks * FILESYSTEM_SD_BLOCK_SIZE;
        if (!filesystem_sd_transfer_prepare_wait())
        {
            return false;
        }

        if (Platform_SD_StartReadBlocks(filesystem_sd_dma_buffer,
                                        current_block,
                                        chunk_blocks) != PLATFORM_OK)
        {
            return false;
        }

        if (!filesystem_sd_transfer_finish(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE,
                                           timeout_ms))
        {
            return false;
        }

        (void)memcpy(destination, filesystem_sd_dma_buffer, byte_count);
        destination += byte_count;
        current_block += chunk_blocks;
        remaining_blocks -= chunk_blocks;
    }

    return true;
}

/**
  * @brief  经 Cache 安全的 DMA bounce buffer 写入一个或多个逻辑 SD 块。
  * @param  source 提供请求块的调用者缓冲区。
  * @param  start_block 第一个待写入的逻辑块。
  * @param  block_count 连续写入的逻辑块数量。
  * @param  timeout_ms 每个 DMA 分块的最长等待时间，单位为毫秒。
  * @retval true 每个块均已到达卡，且卡已回到传输状态。
  * @retval false 输入、所有权、DMA 启动、完成或卡同步失败。
  */
bool filesystem_sd_transfer_write_blocks(const uint8_t *source,
                                         uint32_t start_block,
                                         uint32_t block_count,
                                         uint32_t timeout_ms)
{
    uint32_t remaining_blocks = block_count;
    uint32_t current_block = start_block;

    if ((source == NULL) || (block_count == 0U))
    {
        return false;
    }

    while (remaining_blocks != 0U)
    {
        uint32_t chunk_blocks = remaining_blocks;
        uint32_t byte_count;

        if (chunk_blocks > FILESYSTEM_SD_DMA_BLOCK_COUNT)
        {
            chunk_blocks = FILESYSTEM_SD_DMA_BLOCK_COUNT;
        }

        byte_count = chunk_blocks * FILESYSTEM_SD_BLOCK_SIZE;
        (void)memcpy(filesystem_sd_dma_buffer, source, byte_count);
        if (!filesystem_sd_transfer_prepare_wait())
        {
            return false;
        }

        if (Platform_SD_StartWriteBlocks(filesystem_sd_dma_buffer,
                                         current_block,
                                         chunk_blocks) != PLATFORM_OK)
        {
            return false;
        }

        if (!filesystem_sd_transfer_finish(PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE,
                                           timeout_ms))
        {
            return false;
        }

        source += byte_count;
        current_block += chunk_blocks;
        remaining_blocks -= chunk_blocks;
    }

    return true;
}
