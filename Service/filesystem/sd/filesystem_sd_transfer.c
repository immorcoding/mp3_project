/**
  ******************************************************************************
  * @file    filesystem_sd_transfer.c
  * @brief   在异步 SDMMC DMA 之上执行同步 FatFs 块读写。
  *
  * @details
  *          本 Module 由 Filesystem Service 持有。它把 DMA 完成事件、DMA 可访问的 bounce
  *          buffer、分块和 Platform SD 状态提交隐藏在同步块读写 Interface 后。任何任务都可以调用，
  *          但同一时刻只有持有 SD 卷锁（或全局锁）的那一个：bounce buffer、完成信号量和事件变量
  *          都由那把锁保护（spike #191 去掉了“唯一 Storage Task”的身份绑定）。
  *          完成通知不绑定固定任务：ISR 写事件变量再给一个二值信号量，等待方在启动传输前清空。
  *          Cortex-M7 D-Cache 维护属于 SD STM32 HAL Adapter 的后端 Implementation。
  ******************************************************************************
  */

#include "Service/filesystem/sd/filesystem_sd_transfer.h"
#include "Service/filesystem/filesystem_config.h"

#include <limits.h>
#include <string.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Platform/sd/platform_sd.h"

#if ((FILESYSTEM_SD_BLOCK_SIZE % FILESYSTEM_SD_DMA_BUFFER_ALIGNMENT) != 0U)
#error "SD logical block size must satisfy the SD DMA buffer alignment contract."
#endif

/** @brief DMA 完成事件：ISR 写、持有 SD 卷锁的等待方读；NONE 表示没有事件。 */
static volatile uint32_t filesystem_sd_completion_event = (uint32_t)PLATFORM_SD_TRANSFER_EVENT_NONE;
/** @brief DMA 完成的唤醒提示：二值信号量，静态创建，由 InitSD 路径首次创建。 */
static SemaphoreHandle_t filesystem_sd_completion_semaphore;
static StaticSemaphore_t filesystem_sd_completion_storage;
/** @brief 首次 InitSD 之后为真：信号量已建、传输回调已注册。 */
static bool filesystem_sd_transfer_registered;

static uint8_t filesystem_sd_dma_buffer[
    FILESYSTEM_SD_BLOCK_SIZE * FILESYSTEM_SD_DMA_BLOCK_COUNT]
    __attribute__((aligned(FILESYSTEM_SD_DMA_BUFFER_ALIGNMENT), section(".sd_dma_buffer")));

/**
  * @brief  发布一个 SDMMC 传输事件：写事件变量，再给信号量唤醒当前等待方。
  * @param  event 在 ISR 上下文收到的 Platform 归一化传输结果。
  * @param  context 向 Platform SD 注册的内部上下文，必须是事件变量的地址。
  * @note   此回调有意不访问 DMA buffer、FatFs 或 SDCard 状态机；等待中的任务离开
  *         ISR 上下文后再执行这些操作。不绑定固定任务，谁持有 SD 卷锁谁在等。
  */
static void filesystem_sd_transfer_irq_callback(
    Platform_SD_TransferEventTypeDef event,
    void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if ((context != (void *)&filesystem_sd_completion_event) ||
        (filesystem_sd_completion_semaphore == NULL))
    {
        return;
    }

    filesystem_sd_completion_event = (uint32_t)event;
    (void)xSemaphoreGiveFromISR(filesystem_sd_completion_semaphore, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
  * @brief  启动 DMA 之前清空上一次残留的完成事件与唤醒提示。
  * @retval true 完成信号量已建，可启动一次 DMA。
  * @retval false 尚未 InitSD。
  * @note   调用者已持有 SD 卷锁。迟到的 DMA 完成事件在清空之后到达时仍会被当成本次完成，
  *         这是 #188 第 7 条的既有未决问题，本票不解决。
  */
static bool filesystem_sd_transfer_prepare_wait(void)
{
    if (filesystem_sd_completion_semaphore == NULL)
    {
        return false;
    }

    (void)xSemaphoreTake(filesystem_sd_completion_semaphore, 0U);
    filesystem_sd_completion_event = (uint32_t)PLATFORM_SD_TRANSFER_EVENT_NONE;
    return true;
}

/**
  * @brief  等待恰好一个 Platform SDMMC 传输事件。
  * @param  event 接收 ISR 发布的事件。
  * @param  timeout_ms 本 DMA 分块的最长等待时间，单位为毫秒。
  * @retval true 已收到完整传输事件。
  * @retval false 输出参数无效，或等待超时。
  */
static bool filesystem_sd_transfer_wait(Platform_SD_TransferEventTypeDef *event,
                                        uint32_t timeout_ms)
{
    if ((event == NULL) || (filesystem_sd_completion_semaphore == NULL))
    {
        return false;
    }

    if (xSemaphoreTake(filesystem_sd_completion_semaphore, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return false;
    }

    *event = (Platform_SD_TransferEventTypeDef)filesystem_sd_completion_event;
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
  * @brief  建立 SD DMA 完成信号量并注册传输回调。
  * @param  notify_index 旧版的任务通知槽，已不再使用，保留参数只为不改调用方。
  * @retval true 信号量已建、回调已注册。
  * @retval false Platform SD 尚不能接收订阅者。
  * @note   调用者已持有 SD 卷锁。允许重复调用：挂载和卸载路径把 InitSD 用作幂等的就绪检查。
  *         不再绑定调用任务，也不再检查调用者身份（#188 第 3 条：只加锁不改会让非 owner 调用
  *         被 FatFs 当成未初始化而清掉已挂载卷）。
  */
bool filesystem_sd_transfer_init(uint32_t notify_index)
{
    (void)notify_index;

    if (filesystem_sd_completion_semaphore == NULL)
    {
        filesystem_sd_completion_semaphore = xSemaphoreCreateBinaryStatic(&filesystem_sd_completion_storage);
        if (filesystem_sd_completion_semaphore == NULL)
        {
            return false;
        }
    }

    if (Platform_SD_SetTransferCallback(filesystem_sd_transfer_irq_callback,
                                        (void *)&filesystem_sd_completion_event) != PLATFORM_OK)
    {
        return false;
    }

    filesystem_sd_transfer_registered = true;
    return true;
}

/**
  * @brief  查询 DMA 执行器是否已完成首次 InitSD。
  * @retval true 已完成首次 InitSD。
  */
bool filesystem_sd_transfer_is_registered(void)
{
    return filesystem_sd_transfer_registered && (filesystem_sd_completion_semaphore != NULL);
}

/**
  * @brief  经 Cache 安全的 DMA bounce buffer 读取一个或多个逻辑 SD 块。
  * @param  destination 接收请求块的调用者缓冲区。
  * @param  start_block 第一个待读取的逻辑块。
  * @param  block_count 连续读取的逻辑块数量。
  * @param  timeout_ms 每个 DMA 分块的最长等待时间，单位为毫秒。
  * @retval true DMA 成功后，所有块均已复制到 destination。
  * @retval false 输入、DMA 启动、完成或卡同步失败。
  * @note   调用者已持有 SD 卷锁。
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
  * @retval false 输入、DMA 启动、完成或卡同步失败。
  * @note   调用者已持有 SD 卷锁。
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
