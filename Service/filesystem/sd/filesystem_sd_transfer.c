/**
  ******************************************************************************
  * @file    filesystem_sd_transfer.c
  * @brief   在异步 SDMMC DMA 之上执行同步 FatFs 块读写。
  *
  * @details
 *          本 Module 由 Filesystem Service 持有。它把完成信号量、DMA 可访问的
 *          bounce buffer、分块和 Platform SD 状态提交隐藏在同步块读写 Interface 后。
 *          完成通知是一个静态二值信号量：SDMMC 中断给出，正在执行传输的任务（调用者，
 *          不绑定固定任务）等待。Cortex-M7 D-Cache 维护属于 SD STM32 HAL Adapter 的
 *          后端 Implementation。
  ******************************************************************************
  */

#include "Service/filesystem/sd/filesystem_sd_transfer.h"
#include "Service/filesystem/filesystem_config.h"

#include <limits.h>
#include <string.h>

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Platform/sd/platform_sd.h"

#if ((FILESYSTEM_SD_BLOCK_SIZE % FILESYSTEM_SD_DMA_BUFFER_ALIGNMENT) != 0U)
#error "SD logical block size must satisfy the SD DMA buffer alignment contract."
#endif

/** @brief 完成信号量的静态存储；句柄在第一次 filesystem_sd_transfer_init() 时创建，之后不再重建。 */
static StaticSemaphore_t filesystem_sd_done_storage;
static SemaphoreHandle_t filesystem_sd_done;

/** @brief 中断写入、任务在拿到信号量之后读取的最近一次传输事件（后到者覆盖先到者）。 */
static volatile Platform_SD_TransferEventTypeDef filesystem_sd_event;

static uint8_t filesystem_sd_dma_buffer[
    FILESYSTEM_SD_BLOCK_SIZE * FILESYSTEM_SD_DMA_BLOCK_COUNT]
    __attribute__((aligned(FILESYSTEM_SD_DMA_BUFFER_ALIGNMENT), section(".sd_dma_buffer")));

/**
  * @brief  发布一个 SDMMC 传输事件：记下事件，再给出完成信号量。
  * @param  event 在 ISR 上下文收到的 Platform 归一化传输结果。
  * @param  context 向 Platform SD 注册的完成信号量句柄。
  * @note   运行在 ISR 上下文，只用 FromISR 版本，并把“唤醒了更高优先级任务”交给
  *         portYIELD_FROM_ISR。此回调有意不访问 DMA buffer、FatFs 或 SDCard 状态机；
  *         等待中的任务离开 ISR 上下文后再执行这些操作。同一次等待内后到的事件覆盖先到的
  *         （第二次 Give 对已满的二值信号量无效，但事件值已更新）。
  */
static void filesystem_sd_transfer_irq_callback(
    Platform_SD_TransferEventTypeDef event,
    void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if ((context == NULL) || (context != (void *)filesystem_sd_done))
    {
        return;
    }

    filesystem_sd_event = event;
    (void)xSemaphoreGiveFromISR(filesystem_sd_done, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
  * @brief  启动 DMA 之前丢弃遗留的完成信号。
  * @retval true 执行器已初始化，可启动一次 DMA。
  * @retval false InitSD 尚未建立完成信号量。
  * @note   上一次传输超时后才到达的 DMA 完成中断会让信号量保持置位；若不清掉，下一次
  *         传输会把它当成自己的完成。必须在 Platform_SD_Start*Blocks() 之前清：启动之后
  *         到达的中断属于本次传输，清掉就丢了。清理不覆盖“旧 DMA 在清理之后、启动之前
  *         才完成”的窗口，那由显式 Abort（#209）解决。
  */
static bool filesystem_sd_transfer_prepare_wait(void)
{
    if (filesystem_sd_done == NULL)
    {
        return false;
    }

    (void)xSemaphoreTake(filesystem_sd_done, 0U);
    return true;
}

/**
  * @brief  等待恰好一个 Platform SDMMC 传输事件。
  * @param  event 接收 ISR 发布的事件。
  * @param  timeout_ms 本 DMA 分块的最长等待时间，单位为毫秒。
  * @retval true 已收到完整传输事件。
  * @retval false 输出参数无效、执行器未初始化，或等待超时。
  */
static bool filesystem_sd_transfer_wait(Platform_SD_TransferEventTypeDef *event,
                                        uint32_t timeout_ms)
{
    if ((event == NULL) || (filesystem_sd_done == NULL))
    {
        return false;
    }

    if (xSemaphoreTake(filesystem_sd_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        return false;
    }

    *event = filesystem_sd_event;
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
  * @brief  建立完成信号量（第一次调用时）并向 Platform SD 订阅传输事件。
  * @retval true 信号量就绪且 Platform SD 已保存订阅者。
  * @retval false Platform SD 尚不能接收订阅者（例如还没初始化）。
  * @note   信号量先于订阅创建：Platform 只在订阅之后才会调用 ISR 回调，所以 ISR 看到的
  *         信号量总是已经存在的。重复调用（挂载和卸载路径把 InitSD 的内部版本
  *         用作幂等的就绪检查）只重新订阅，不重建信号量，也不动其中的信号。调用者总是已持
  *         SD 卷锁，所以不会与其他调用并发。
  */
bool filesystem_sd_transfer_init(void)
{
    if (filesystem_sd_done == NULL)
    {
        filesystem_sd_done = xSemaphoreCreateBinaryStatic(&filesystem_sd_done_storage);
        if (filesystem_sd_done == NULL)
        {
            return false;
        }
    }

    return Platform_SD_SetTransferCallback(filesystem_sd_transfer_irq_callback,
                                           (void *)filesystem_sd_done) == PLATFORM_OK;
}

/**
  * @brief  查询 DMA 执行器是否已经初始化（完成信号量已建立）。
  * @retval true 已调用过 filesystem_sd_transfer_init()。
  */
bool filesystem_sd_transfer_is_ready(void)
{
    return filesystem_sd_done != NULL;
}

/**
  * @brief  经 Cache 安全的 DMA bounce buffer 读取一个或多个逻辑 SD 块。
  * @param  destination 接收请求块的调用者缓冲区。
  * @param  start_block 第一个待读取的逻辑块。
  * @param  block_count 连续读取的逻辑块数量。
  * @param  timeout_ms 每个 DMA 分块的最长等待时间，单位为毫秒。
  * @retval true DMA 成功后，所有块均已复制到 destination。
  * @retval false 输入、执行器未初始化、DMA 启动、完成或卡同步失败。
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
  * @retval false 输入、执行器未初始化、DMA 启动、完成或卡同步失败。
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
        if (!filesystem_sd_transfer_prepare_wait())
        {
            return false;
        }

        (void)memcpy(filesystem_sd_dma_buffer, source, byte_count);
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
