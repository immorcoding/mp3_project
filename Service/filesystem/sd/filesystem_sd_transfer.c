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
 *
 *          等待超时不再只是把 Device 标成 ERROR：先向 Platform 提交有上界的显式中止，让 DMA 真正停下来，
 *          再清完成信号量（#209）。迟到 DMA 测试钩子（FILESYSTEM_HOOK_SD_LATE_DMA，默认关闭）也在本文件。
  ******************************************************************************
  */

#include "Service/filesystem/sd/filesystem_sd_transfer.h"
#include "Service/filesystem/filesystem_config.h"

#include <limits.h>
#include <string.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Platform/sd/platform_sd.h"
#include "Service/log/log_service.h"

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

/** @brief 中止结果日志行的静态缓冲（128 字节与 Service Log 单条正文容量相同）；只在持 SD 卷锁的传输里使用，归 SD 卷锁。 */
static char filesystem_sd_log_line[FILESYSTEM_SD_LOG_LINE_BYTES];

/** @brief 一行日志的拼接状态。 */
typedef struct
{
    char *buffer;
    uint32_t capacity;
    uint32_t length;
} filesystem_sd_line_t;

/**
  * @brief  往行尾追加文本，装不下就截断。
  * @param[in,out] line 拼接状态。
  * @param[in] text 以 '\0' 结尾的文本。
  */
static void filesystem_sd_line_text(filesystem_sd_line_t *line, const char *text)
{
    while ((*text != '\0') && ((line->length + 1U) < line->capacity))
    {
        line->buffer[line->length++] = *text++;
    }
    line->buffer[line->length] = '\0';
}

/**
  * @brief  往行尾追加一个十进制无符号数。不用 snprintf：它在调用者的栈上要几百字节。
  * @param[in,out] line 拼接状态。
  * @param[in] value 数值。
  */
static void filesystem_sd_line_u32(filesystem_sd_line_t *line, uint32_t value)
{
    char digits[10];
    uint32_t count = 0U;

    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    while ((count != 0U) && ((line->length + 1U) < line->capacity))
    {
        line->buffer[line->length++] = digits[--count];
    }
    line->buffer[line->length] = '\0';
}

/**
  * @brief  中止结果的短名，写进 FSABORT 日志行。
  * @param[in] result Platform 报告的中止结果。
  * @return 静态字符串。
  */
static const char *filesystem_sd_abort_result_name(Platform_SD_AbortResultTypeDef result)
{
    switch (result)
    {
        case PLATFORM_SD_ABORT_STOPPED:
            return "stopped";

        case PLATFORM_SD_ABORT_NO_MEDIA:
            return "no_media";

        case PLATFORM_SD_ABORT_STUCK:
            return "stuck";

        case PLATFORM_SD_ABORT_REJECTED:
        default:
            return "rejected";
    }
}

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
  *         到达的中断属于本次传输，清掉就丢了。单靠清理清不到“旧 DMA 在清理之后、启动之前
  *         才完成”的窗口；那个窗口由顺序关掉：每次等待超时都先显式中止让硬件静止、再清信号量，
  *         下一笔 Start 只能在这之后（见 filesystem_sd_transfer_abort_after_timeout）。这里的清理
  *         作为第二道保险保留。
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
  * @brief  自某个起点起经过的微秒数。
  * @param[in] start_cycles 起点的周期计数。
  * @param[in] start_tick 起点的 tick。
  * @return 周期计数器可用（频率非 0）时按周期计数算，微秒精度；否则按 tick 算，毫秒精度。
  * @note   中止的上界远小于周期计数器的回绕时间（480 MHz 下约 8.9 s），所以不需要处理回绕。
  */
static uint32_t filesystem_sd_elapsed_us(uint32_t start_cycles, TickType_t start_tick)
{
    const uint32_t cycles_per_us = CortexM7CycleCounter_GetFrequencyHz() / 1000000U;

    if (cycles_per_us == 0U)
    {
        return (uint32_t)((TickType_t)(xTaskGetTickCount() - start_tick) * (1000000U / (uint32_t)configTICK_RATE_HZ));
    }

    return (CortexM7CycleCounter_Read() - start_cycles) / cycles_per_us;
}

/**
  * @brief  等待超时后显式中止这一笔 DMA，并记一行 WARN。
  * @param[in] is_read true 为读方向，false 为写方向（只用于日志）。
  * @param[in] start_block 这一分块的起始块（只用于日志）。
  * @param[in] block_count 这一分块的块数（只用于日志）。
  * @note   日志行：`FSABORT <结果> us=<微秒> <R|W> blk=<起始块> n=<块数>`，结果为 stopped、no_media、stuck、
  *         rejected。日志在持 SD 卷锁时投递：`Service_Log_Post` 不阻塞，可以调用；每次超时最多一行。
  */
static void filesystem_sd_transfer_abort_after_timeout(bool is_read,
                                                       uint32_t start_block,
                                                       uint32_t block_count)
{
    Platform_SD_AbortResultTypeDef result = PLATFORM_SD_ABORT_REJECTED;
    const uint32_t start_cycles = CortexM7CycleCounter_Read();
    const TickType_t start_tick = xTaskGetTickCount();
    filesystem_sd_line_t line = {filesystem_sd_log_line, FILESYSTEM_SD_LOG_LINE_BYTES, 0U};

    (void)Platform_SD_AbortTransfer(&result);
    if (result == PLATFORM_SD_ABORT_REJECTED)
    {
        /* Platform 没有受理（没有在飞的传输，或 Port 不支持中止）：退回中止之前的做法，让 Device 进入 ERROR。 */
        (void)Platform_SD_CompleteTransfer(PLATFORM_SD_TRANSFER_EVENT_ERROR);
    }

    /* 中止返回之后硬件已静止（STOPPED：数据通路空闲且中断屏蔽；其余结果：SDMMC 已断电），不会再有这一笔的中断。
     * 只有中止期间或之前落进来的那一次完成留下了信号与事件值，这里清掉；顺序是“硬件先停、信号后清”，所以
     * 之后到下一笔 Start 之间没有任何旧信号能再出现——这就关掉了 prepare_wait 清不到的那个窗口。 */
    (void)xSemaphoreTake(filesystem_sd_done, 0U);
    filesystem_sd_event = PLATFORM_SD_TRANSFER_EVENT_NONE;

    filesystem_sd_line_text(&line, filesystem_sd_abort_result_name(result));
    filesystem_sd_line_text(&line, " us=");
    filesystem_sd_line_u32(&line, filesystem_sd_elapsed_us(start_cycles, start_tick));
    filesystem_sd_line_text(&line, is_read ? " R blk=" : " W blk=");
    filesystem_sd_line_u32(&line, start_block);
    filesystem_sd_line_text(&line, " n=");
    filesystem_sd_line_u32(&line, block_count);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, "FSABORT", filesystem_sd_log_line);
}

/**
  * @brief  将收到的 DMA 事件提交给 Platform SD 状态机。
  * @param[in] expected_event 当前操作方向所要求的完成事件。
  * @param[in] timeout_ms 本 DMA 分块的最长等待时间，单位为毫秒。
  * @param[in] start_block 这一分块的起始块（超时后写日志用）。
  * @param[in] block_count 这一分块的块数（超时后写日志用）。
  * @retval true DMA 与传输后的 SD 卡同步均成功。
  * @retval false 超时（已显式中止）、错误、方向不符或同步失败。
  */
static bool filesystem_sd_transfer_finish(
    Platform_SD_TransferEventTypeDef expected_event,
    uint32_t timeout_ms,
    uint32_t start_block,
    uint32_t block_count)
{
    Platform_SD_TransferEventTypeDef event;

    if (!filesystem_sd_transfer_wait(&event, timeout_ms))
    {
        filesystem_sd_transfer_abort_after_timeout(
            expected_event == PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE,
            start_block,
            block_count);
        return false;
    }

    if (event != expected_event)
    {
        (void)Platform_SD_CompleteTransfer(event);
        return false;
    }

    return Platform_SD_CompleteTransfer(event) == PLATFORM_OK;
}

/** @brief 迟到 DMA 钩子：读请求计数，归 SD 卷锁（只在持锁的传输里读写）。 */
static uint32_t filesystem_sd_hook_reads;

/** @brief 迟到 DMA 钩子：已做的探测次数，归 SD 卷锁。 */
static uint32_t filesystem_sd_hook_probes;

/**
  * @brief  对一段字节算 FNV-1a 32 位校验和，供钩子比较两次读的内容。
  * @param[in] data 字节序列。
  * @param[in] length 字节数。
  * @return 校验和。
  */
static uint32_t filesystem_sd_hook_checksum(const uint8_t *data, uint32_t length)
{
    uint32_t hash = 2166136261U;

    for (uint32_t index = 0U; index < length; index++)
    {
        hash ^= data[index];
        hash *= 16777619U;
    }

    return hash;
}

/**
  * @brief  钩子打开时，在启动日志里投一行说明开了什么（DIAG-7：没有 HOOK 行就是没开）。
  * @note   只在第一次建立信号量时投一次；钩子关闭时这个函数什么也不做。
  */
static void filesystem_sd_hook_announce(void)
{
    filesystem_sd_line_t line = {filesystem_sd_log_line, FILESYSTEM_SD_LOG_LINE_BYTES, 0U};

    if (FILESYSTEM_HOOK_SD_LATE_DMA != 0U)
    {
        filesystem_sd_line_text(&line, "late_dma ON period=");
        filesystem_sd_line_u32(&line, FILESYSTEM_HOOK_SD_LATE_DMA_PERIOD);
        filesystem_sd_line_text(&line, " timeout=");
        filesystem_sd_line_u32(&line, FILESYSTEM_HOOK_SD_LATE_DMA_TIMEOUT_MS);
        filesystem_sd_line_text(&line, "ms");
        (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, "HOOK", filesystem_sd_log_line);
    }
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

        filesystem_sd_hook_announce();
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
  * @brief  读卡检测脚：槽里现在有没有卡。
  * @retval true 卡检测输入表示有卡。
  * @retval false 表示没有卡，或 Platform 尚未绑定。
  * @note   只读一次 GPIO 输入寄存器（经 Platform，不碰 SDMMC、不改任何状态），任务上下文安全，不阻塞。
  *         调用者在 SD 卷锁内调用，让“读写失败”和“卡不在”是同一刻的结论。
  */
bool filesystem_sd_transfer_card_present(void)
{
    return Platform_SD_IsPresent();
}

/**
  * @brief  把不超过一个 DMA 中转缓冲的连续块读进 bounce buffer，不复制给调用者。
  * @param[in] start_block 第一个待读取的逻辑块。
  * @param[in] chunk_blocks 块数，不超过 FILESYSTEM_SD_DMA_BLOCK_COUNT。
  * @param[in] timeout_ms 这个分块的最长等待时间，单位为毫秒。
  * @retval true DMA 成功，数据在 bounce buffer 里（读方向的 Cache 已作废）。
  * @retval false 执行器未初始化、DMA 启动被拒、等待超时（已显式中止）、错误或卡同步失败。
  */
static bool filesystem_sd_transfer_read_chunk(uint32_t start_block,
                                              uint32_t chunk_blocks,
                                              uint32_t timeout_ms)
{
    if (!filesystem_sd_transfer_prepare_wait())
    {
        return false;
    }

    if (Platform_SD_StartReadBlocks(filesystem_sd_dma_buffer,
                                    start_block,
                                    chunk_blocks) != PLATFORM_OK)
    {
        return false;
    }

    return filesystem_sd_transfer_finish(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE,
                                         timeout_ms,
                                         start_block,
                                         chunk_blocks);
}

/**
  * @brief  迟到 DMA 钩子：每 PERIOD 次读请求探测一次，在调用者自己的读之前。
  * @param[in] start_block 调用者这次读的起始块，也是探测的目标块。
  * @param[in] block_count 调用者这次读的块数。
  * @param[in] timeout_ms 调用者这次读的单块等待上限，探测里的正常读沿用。
  * @note   仅在 FILESYSTEM_HOOK_SD_LATE_DMA 打开时被调用，由持 SD 卷锁的传输调用（会拉长这次读的持锁时间约 15 ms，
  *         所以持锁统计的数字在钩子打开的构建里不能当 R2.1 证据）。探测：
  *         1. 读目标块，记校验和；
  *         2. 从块 0 读 32 KiB 但只等 TIMEOUT_MS，必然超时，被显式中止（FSABORT 那一行 WARN 照常出现）；
  *         3. 紧接着再读目标块，校验和必须与第一次一致——迟到的旧 DMA 若落进 bounce buffer，内容会变；
  *         最后记一行 `SDHOOK late_dma n=<次数> blk=<目标块> victim=<timeout|done|none> verify=<ok|MISMATCH|read_failed|skipped>`。
  *         受害块（块 0 一带）与目标块内容不同，目标块不低于 MIN_BLOCK，所以内容被换成受害块时能看出来。
  */
static void filesystem_sd_hook_late_dma_probe(uint32_t start_block,
                                              uint32_t block_count,
                                              uint32_t timeout_ms)
{
    const uint32_t chunk_blocks = (block_count > FILESYSTEM_SD_DMA_BLOCK_COUNT) ? FILESYSTEM_SD_DMA_BLOCK_COUNT : block_count;
    const uint32_t byte_count = chunk_blocks * FILESYSTEM_SD_BLOCK_SIZE;
    const char *victim = "none";
    const char *verify = "skipped";
    Service_Log_LevelTypeDef level = SERVICE_LOG_LEVEL_WARN;
    filesystem_sd_line_t line = {filesystem_sd_log_line, FILESYSTEM_SD_LOG_LINE_BYTES, 0U};

    filesystem_sd_hook_reads++;
    if (((filesystem_sd_hook_reads % FILESYSTEM_HOOK_SD_LATE_DMA_PERIOD) != 0U) ||
        (start_block < FILESYSTEM_HOOK_SD_LATE_DMA_MIN_BLOCK) ||
        (chunk_blocks == 0U))
    {
        return;
    }

    filesystem_sd_hook_probes++;
    if (filesystem_sd_transfer_read_chunk(start_block, chunk_blocks, timeout_ms))
    {
        const uint32_t checksum_before = filesystem_sd_hook_checksum(filesystem_sd_dma_buffer, byte_count);

        victim = filesystem_sd_transfer_read_chunk(0U,
                                                   FILESYSTEM_SD_DMA_BLOCK_COUNT,
                                                   FILESYSTEM_HOOK_SD_LATE_DMA_TIMEOUT_MS)
                     ? "done"
                     : "timeout";
        if (!filesystem_sd_transfer_read_chunk(start_block, chunk_blocks, timeout_ms))
        {
            verify = "read_failed";
            level = SERVICE_LOG_LEVEL_ERROR;
        }
        else if (filesystem_sd_hook_checksum(filesystem_sd_dma_buffer, byte_count) != checksum_before)
        {
            verify = "MISMATCH";
            level = SERVICE_LOG_LEVEL_ERROR;
        }
        else
        {
            verify = "ok";
            level = SERVICE_LOG_LEVEL_INFO;
        }
    }

    filesystem_sd_line_text(&line, "late_dma n=");
    filesystem_sd_line_u32(&line, filesystem_sd_hook_probes);
    filesystem_sd_line_text(&line, " blk=");
    filesystem_sd_line_u32(&line, start_block);
    filesystem_sd_line_text(&line, " victim=");
    filesystem_sd_line_text(&line, victim);
    filesystem_sd_line_text(&line, " verify=");
    filesystem_sd_line_text(&line, verify);
    (void)Service_Log_Post(level, "SDHOOK", filesystem_sd_log_line);
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

    if (FILESYSTEM_HOOK_SD_LATE_DMA != 0U)
    {
        filesystem_sd_hook_late_dma_probe(start_block, block_count, timeout_ms);
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
        if (!filesystem_sd_transfer_read_chunk(current_block, chunk_blocks, timeout_ms))
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
                                           timeout_ms,
                                           current_block,
                                           chunk_blocks))
        {
            return false;
        }

        source += byte_count;
        current_block += chunk_blocks;
        remaining_blocks -= chunk_blocks;
    }

    return true;
}
