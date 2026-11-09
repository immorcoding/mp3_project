/**
 * @file test_filesystem_completion.c
 * @brief SD 与 Flash 完成等待：成功、超时、错误、迟到的旧信号，以及不依赖调用任务身份。
 *
 * 真实的 Filesystem Service、SD/Flash 私有执行器、FatFs 与 USER DiskIO 胶水，加共用 FreeRTOS 桩
 * （二值信号量、阻塞钩子、tick）；只有 Platform 层用主机替身。SD 传输事件与 Flash 异步操作由
 * 阻塞钩子在“等待将要发生”时替中断发出，所以主机测试能走完“启动 → 等待 → 收尾”的各个分支，
 * 但不证明真实 ISR 时序、优先级与 DMA 行为（留给板级验收）。
 */

#include "Service/filesystem/filesystem_config.h"
#include "Service/filesystem/filesystem_file.h"
#include "Service/filesystem/filesystem_flash_access.h"
#include "Service/filesystem/filesystem_service.h"
#include "Service/filesystem/flash/filesystem_flash_config.h"
#include "Service/filesystem/sd/filesystem_sd_transfer.h"

#include "FATFS/App/fatfs.h"
#include "FATFS/Target/bsp_driver_user_diskio.h"
#include "Platform/flash/platform_flash.h"
#include "Platform/sd/platform_sd.h"
#include "Service/log/log_service.h"

#include "freertos_stub.h"
#include "lock_env_stub.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

char SDPath[4];
char USERPath[4];
uint8_t retSD;
uint8_t retUSER;
FATFS SDFatFS;
FATFS USERFatFS;

/* ------------------------------------------------------------------------- */
/* Platform SD 替身                                                           */
/* ------------------------------------------------------------------------- */

#define SD_BLOCK_BYTES FILESYSTEM_SD_BLOCK_SIZE
#define SD_MAX_EVENTS 4U
#define SD_MAX_COMPLETIONS 64U
#define SD_MAX_TEST_BLOCKS (FILESYSTEM_SD_DMA_BLOCK_COUNT * 2U + 1U)

/** @brief Platform SD 替身的脚本与观察值。 */
typedef struct
{
    Platform_SD_TransferCallback_t callback;                           /**< 当前订阅者。 */
    void *context;                                                     /**< 订阅者上下文。 */
    uint32_t set_callback_calls;                                       /**< 订阅调用次数。 */
    bool semaphore_existed_when_callback_set;                          /**< 订阅时执行器的信号量是否已创建。 */
    Platform_StatusTypeDef start_status;                               /**< Start 的返回值。 */
    Platform_SD_TransferEventTypeDef irq_events[SD_MAX_EVENTS];         /**< 每次等待时按序发出的中断事件。 */
    uint32_t irq_event_count;                                          /**< 0 表示没有中断（等待超时）。 */
    bool irq_every_wait;                                               /**< false 时只在第一次等待发出。 */
    uint32_t start_calls;                                              /**< Start 次数。 */
    uint8_t written[SD_MAX_TEST_BLOCKS];                               /**< 写入各块的首字节。 */
    uint32_t written_blocks;                                           /**< 已记录的写入块数。 */
    Platform_SD_TransferEventTypeDef completed[SD_MAX_COMPLETIONS];     /**< CompleteTransfer 收到的事件。 */
    uint32_t complete_calls;                                           /**< CompleteTransfer 次数。 */
    Platform_StatusTypeDef complete_status;                            /**< CompleteTransfer 的返回值。 */

    /* 显式中止（#209）：脚本与观察。以下字段由 begin_test 清零。 */
    Platform_SD_AbortResultTypeDef abort_result;                       /**< AbortTransfer 报告的结果；0 即 STOPPED。 */
    uint32_t abort_us;                                                 /**< 中止“花”的微秒（推进假周期计数器）。 */
    uint32_t abort_ticks;                                              /**< 中止“花”的 tick（用来走周期计数器不可用时的兜底）。 */
    bool abort_fires_late_completion;                                  /**< 中止期间有一次读完成中断落进来。 */
    uint32_t abort_calls;                                              /**< AbortTransfer 次数。 */
    bool device_failed;                                                /**< 中止没有停住后 Device 保持 ERROR：之后的 Start 被拒绝。 */
    char sequence[128];                                                /**< 对 Platform 的调用顺序：S 启动、A 中止、C 提交事件。 */
    Platform_SD_TransferEventTypeDef wait_plan[8];                     /**< 按等待次序给的中断事件；NONE 表示这次等待没有中断（超时）。 */
    uint32_t wait_plan_len;                                            /**< 0 表示不用计划，沿用 irq_events 脚本。 */
    uint32_t wait_index;                                               /**< 已经消耗的计划项数。 */
    bool corrupt_read_after_abort;                                     /**< 中止之后的下一笔读，缓冲首块被“迟到的旧 DMA”写坏。 */
} FakeSd;

static FakeSd fake_sd;
static SemaphoreHandle_t sd_done;

/**
 * @brief 把一次对 Platform 的调用记进顺序串。
 * @param[in] call 'S' 启动、'A' 中止、'C' 提交事件。
 */
static void sd_sequence_add(char call)
{
    size_t length = strlen(fake_sd.sequence);

    assert(length + 1U < sizeof(fake_sd.sequence));
    fake_sd.sequence[length] = call;
    fake_sd.sequence[length + 1U] = '\0';
}

/**
 * @brief 订阅传输事件：记录回调，并记下此刻执行器的信号量是否已经存在。
 * @param[in] transfer_callback ISR 回调。
 * @param[in] transfer_context 回调上下文。
 * @return 固定 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_SD_SetTransferCallback(
    Platform_SD_TransferCallback_t transfer_callback,
    void *transfer_context)
{
    assert(transfer_callback != NULL);
    fake_sd.callback = transfer_callback;
    fake_sd.context = transfer_context;
    fake_sd.set_callback_calls++;
    fake_sd.semaphore_existed_when_callback_set = freertos_stub_last_created_semaphore() != NULL;
    return PLATFORM_OK;
}

/**
 * @brief 启动 DMA 读：按脚本返回；成功时向缓冲写入“块号”图样。
 * @param[out] data DMA 缓冲。
 * @param[in] start_block 起始块。
 * @param[in] block_count 块数。
 * @return 脚本给定的状态。
 */
Platform_StatusTypeDef Platform_SD_StartReadBlocks(uint8_t *data,
                                                   uint32_t start_block,
                                                   uint32_t block_count)
{
    fake_sd.start_calls++;
    sd_sequence_add('S');
    if (fake_sd.device_failed)
    {
        return PLATFORM_SD_ERROR;
    }
    if (fake_sd.start_status != PLATFORM_OK)
    {
        return fake_sd.start_status;
    }
    for (uint32_t index = 0U; index < block_count; index++)
    {
        memset(&data[index * SD_BLOCK_BYTES], (int)(uint8_t)(start_block + index), SD_BLOCK_BYTES);
    }
    if (fake_sd.corrupt_read_after_abort && (fake_sd.abort_calls > 0U))
    {
        memset(data, 0xEE, SD_BLOCK_BYTES);
        fake_sd.corrupt_read_after_abort = false;
    }
    return PLATFORM_OK;
}

/**
 * @brief 启动 DMA 写：按脚本返回；成功时记录各块首字节。
 * @param[in] data DMA 缓冲。
 * @param[in] start_block 起始块（未使用）。
 * @param[in] block_count 块数。
 * @return 脚本给定的状态。
 */
Platform_StatusTypeDef Platform_SD_StartWriteBlocks(const uint8_t *data,
                                                    uint32_t start_block,
                                                    uint32_t block_count)
{
    (void)start_block;
    fake_sd.start_calls++;
    sd_sequence_add('S');
    if (fake_sd.device_failed)
    {
        return PLATFORM_SD_ERROR;
    }
    if (fake_sd.start_status != PLATFORM_OK)
    {
        return fake_sd.start_status;
    }
    for (uint32_t index = 0U; index < block_count; index++)
    {
        assert(fake_sd.written_blocks < SD_MAX_TEST_BLOCKS);
        fake_sd.written[fake_sd.written_blocks++] = data[index * SD_BLOCK_BYTES];
    }
    return PLATFORM_OK;
}

/**
 * @brief 记录执行器在普通上下文提交的事件。
 * @param[in] event 提交给 Platform 的传输事件。
 * @return 脚本给定的状态。
 */
Platform_StatusTypeDef Platform_SD_CompleteTransfer(Platform_SD_TransferEventTypeDef event)
{
    assert(fake_sd.complete_calls < SD_MAX_COMPLETIONS);
    fake_sd.completed[fake_sd.complete_calls++] = event;
    sd_sequence_add('C');
    return fake_sd.complete_status;
}

/**
 * @brief 卡检测脚替身：本测试不涉及读写失败后的分类，恒表示有卡。
 * @return 恒为 true。
 */
bool Platform_SD_IsPresent(void)
{
    return true;
}

/**
 * @brief 显式中止：按脚本“花时间”、可选地让一次读完成中断落进中止期间，报告脚本里的结果。
 * @param[out] result 结果分类，可为空。
 * @return STOPPED 时 PLATFORM_OK，其余 PLATFORM_SD_ERROR；没有停住时 Device 保持 ERROR，之后的 Start 被拒绝。
 */
Platform_StatusTypeDef Platform_SD_AbortTransfer(Platform_SD_AbortResultTypeDef *result)
{
    fake_sd.abort_calls++;
    sd_sequence_add('A');
    lock_env_advance_us(fake_sd.abort_us);
    if (fake_sd.abort_ticks != 0U)
    {
        vTaskDelay(fake_sd.abort_ticks);
    }
    if (fake_sd.abort_fires_late_completion && (fake_sd.callback != NULL))
    {
        fake_sd.callback(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, fake_sd.context);
    }
    if (result != NULL)
    {
        *result = fake_sd.abort_result;
    }
    if (fake_sd.abort_result != PLATFORM_SD_ABORT_STOPPED)
    {
        fake_sd.device_failed = true;
        return PLATFORM_SD_ERROR;
    }
    return PLATFORM_OK;
}

/**
 * @brief 替“SDMMC 中断”按脚本向已订阅的回调发事件。
 */
static void sd_fire_scripted_irqs(void)
{
    uint32_t count = fake_sd.irq_event_count;

    if (fake_sd.wait_plan_len != 0U)
    {
        if (fake_sd.wait_index < fake_sd.wait_plan_len)
        {
            const Platform_SD_TransferEventTypeDef planned = fake_sd.wait_plan[fake_sd.wait_index++];

            if ((planned != PLATFORM_SD_TRANSFER_EVENT_NONE) && (fake_sd.callback != NULL))
            {
                fake_sd.callback(planned, fake_sd.context);
            }
        }
        return;
    }

    if (!fake_sd.irq_every_wait)
    {
        fake_sd.irq_event_count = 0U;
    }
    for (uint32_t index = 0U; (index < count) && (fake_sd.callback != NULL); index++)
    {
        fake_sd.callback(fake_sd.irq_events[index], fake_sd.context);
    }
}

/* ------------------------------------------------------------------------- */
/* Platform Flash 替身                                                        */
/* ------------------------------------------------------------------------- */

#define FLASH_SECTOR_COUNT 2048U
#define FLASH_SECTOR_BYTES 512U

/** @brief Platform Flash 替身：RAM 盘加可脚本化的异步操作。 */
typedef struct
{
    Platform_Flash_OperationCallback_t callback;      /**< 当前订阅者。 */
    void *context;                                    /**< 订阅者上下文。 */
    uint32_t set_callback_calls;                      /**< 订阅调用次数。 */
    bool semaphore_existed_when_callback_set;         /**< 订阅时执行器的信号量是否已创建。 */
    Platform_Flash_VolumeStateTypeDef volume_state;   /**< 卷状态。 */
    uint8_t media[FLASH_SECTOR_COUNT][FLASH_SECTOR_BYTES]; /**< RAM 盘。 */

    /* 脚本：作用于之后启动的每一个操作，直到测试改写。 */
    Platform_StatusTypeDef start_status;              /**< Start 的返回值。 */
    int32_t busy_polls;                               /**< Process 返回 BUSY 的次数；-1 表示自己永不完成。 */
    int32_t abort_busy_polls;                         /**< Abort 之后 Process 仍返回 BUSY 的次数。 */
    Platform_StatusTypeDef final_status;              /**< 正常结束时 Process 的返回值。 */
    bool irq_every_wait;                              /**< 每次等待是否发出完成中断。 */

    /* 当前操作。 */
    bool active;                                      /**< 有操作在飞。 */
    int32_t polls_left;                               /**< 还要返回 BUSY 的次数。 */
    bool aborted;                                     /**< 已调用 Abort。 */

    /* 观察。 */
    uint32_t starts;                                  /**< Start 总次数。 */
    uint32_t process_calls;                           /**< Process 总次数。 */
    uint32_t process_calls_after_abort;               /**< Abort 之后的 Process 次数。 */
    uint32_t abort_calls;                             /**< Abort 次数。 */
} FakeFlash;

static FakeFlash fake_flash;
static SemaphoreHandle_t flash_done;

/**
 * @brief 订阅异步操作事件：记录回调，并记下此刻执行器的信号量是否已经存在。
 * @param[in] operation_callback ISR 回调。
 * @param[in] operation_context 回调上下文。
 * @return 固定 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_SetOperationCallback(
    Platform_Flash_OperationCallback_t operation_callback,
    void *operation_context)
{
    assert(operation_callback != NULL);
    fake_flash.callback = operation_callback;
    fake_flash.context = operation_context;
    fake_flash.set_callback_calls++;
    fake_flash.semaphore_existed_when_callback_set = freertos_stub_last_created_semaphore() != NULL;
    return PLATFORM_OK;
}

/**
 * @brief 按脚本启动一个异步操作。
 * @return 脚本给定的 Start 结果；成功时登记在飞操作。
 */
static Platform_StatusTypeDef flash_begin_operation(void)
{
    fake_flash.starts++;
    if (fake_flash.start_status != PLATFORM_OK)
    {
        return fake_flash.start_status;
    }
    assert(!fake_flash.active);
    fake_flash.active = true;
    fake_flash.aborted = false;
    fake_flash.polls_left = fake_flash.busy_polls;
    return PLATFORM_OK;
}

/**
 * @brief 启动一次原始数组读取：向缓冲写入固定图样，并按脚本登记在飞操作。
 * @param[in] address 起始地址（未使用）。
 * @param[out] data 接收缓冲。
 * @param[in] data_length 字节数。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_StartReadArray(uint32_t address, uint8_t *data, uint32_t data_length)
{
    (void)address;
    memset(data, 0xA5, data_length);
    return flash_begin_operation();
}

/**
 * @brief 启动一次诊断读取：向缓冲写入固定图样，并按脚本登记在飞操作。
 * @param[in] region 诊断区域（未使用）。
 * @param[out] data 接收缓冲。
 * @param[in] data_length 字节数。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticRead(Platform_Flash_DiagnosticRegionTypeDef region,
                                                          uint8_t *data,
                                                          uint32_t data_length)
{
    (void)region;
    memset(data, 0x5A, data_length);
    return flash_begin_operation();
}

/**
 * @brief 启动一次诊断擦除：只按脚本登记在飞操作。
 * @param[in] region 诊断区域（未使用）。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticErase(Platform_Flash_DiagnosticRegionTypeDef region)
{
    (void)region;
    return flash_begin_operation();
}

/**
 * @brief 启动一次诊断页编程：只按脚本登记在飞操作。
 * @param[in] region 诊断区域（未使用）。
 * @param[in] page_offset 页内偏移（未使用）。
 * @param[in] data 页数据（未使用）。
 * @param[in] data_length 字节数（未使用）。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_StartDiagnosticPageProgram(
    Platform_Flash_DiagnosticRegionTypeDef region,
    uint32_t page_offset,
    const uint8_t *data,
    uint32_t data_length)
{
    (void)region;
    (void)page_offset;
    (void)data;
    (void)data_length;
    return flash_begin_operation();
}

/**
 * @brief 绑定逻辑卷：恒成功。
 * @return 固定 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_BindVolume(void)
{
    return PLATFORM_OK;
}

/**
 * @brief 启动打开卷：按脚本登记在飞操作，成功时卷进入 READY。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_OpenVolumeStart(void)
{
    Platform_StatusTypeDef status = flash_begin_operation();

    if (status == PLATFORM_OK)
    {
        fake_flash.volume_state = PLATFORM_FLASH_VOLUME_READY;
    }
    return status;
}

/**
 * @brief 启动格式化：按脚本登记在飞操作，成功时清空 RAM 盘并进入 READY。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_FormatVolumeStart(void)
{
    Platform_StatusTypeDef status = flash_begin_operation();

    if (status == PLATFORM_OK)
    {
        memset(fake_flash.media, 0, sizeof(fake_flash.media));
        fake_flash.volume_state = PLATFORM_FLASH_VOLUME_READY;
    }
    return status;
}

/**
 * @brief 启动逻辑扇区读取：成功时立刻从 RAM 盘复制，完成时机由脚本决定。
 * @param[in] lba 起始扇区。
 * @param[out] data 接收缓冲。
 * @param[in] count 扇区数。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_ReadBlocksStart(uint32_t lba, uint8_t *data, uint32_t count)
{
    Platform_StatusTypeDef status = flash_begin_operation();

    if (status == PLATFORM_OK)
    {
        memcpy(data, fake_flash.media[lba], count * FLASH_SECTOR_BYTES);
    }
    return status;
}

/**
 * @brief 启动逻辑扇区写入：成功时立刻写入 RAM 盘，完成时机由脚本决定。
 * @param[in] lba 起始扇区。
 * @param[in] data 源缓冲。
 * @param[in] count 扇区数。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_WriteBlocksStart(uint32_t lba, const uint8_t *data, uint32_t count)
{
    Platform_StatusTypeDef status = flash_begin_operation();

    if (status == PLATFORM_OK)
    {
        memcpy(fake_flash.media[lba], data, count * FLASH_SECTOR_BYTES);
    }
    return status;
}

/**
 * @brief 启动同步：只按脚本登记在飞操作。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_SyncVolumeStart(void)
{
    return flash_begin_operation();
}

/**
 * @brief 启动回收：只按脚本登记在飞操作。
 * @return 脚本给定的 Start 结果。
 */
Platform_StatusTypeDef Platform_Flash_ReclaimVolumeStart(void)
{
    return flash_begin_operation();
}

/**
 * @brief 返回替身的卷状态。
 * @return 当前卷状态。
 */
Platform_Flash_VolumeStateTypeDef Platform_Flash_GetVolumeState(void)
{
    return fake_flash.volume_state;
}

/**
 * @brief 返回替身 RAM 盘的几何。
 * @param[out] info 几何输出。
 * @return 固定 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_GetVolumeInfo(Platform_Flash_VolumeInfoTypeDef *info)
{
    info->SectorCount = FLASH_SECTOR_COUNT;
    info->SectorBytes = FLASH_SECTOR_BYTES;
    info->PhysicalBytes = FLASH_SECTOR_COUNT * FLASH_SECTOR_BYTES;
    return PLATFORM_OK;
}

/**
 * @brief 返回全零的卷诊断（产品默认不启用诊断日志，这里只为链接）。
 * @param[out] diagnostics 诊断输出。
 * @return 固定 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_GetVolumeDiagnostics(Platform_Flash_VolumeDiagnosticsTypeDef *diagnostics)
{
    memset(diagnostics, 0, sizeof(*diagnostics));
    return PLATFORM_OK;
}

/**
 * @brief 恢复器件：恒成功。
 * @return 固定 PLATFORM_OK。
 */
Platform_StatusTypeDef Platform_Flash_RecoverVolume(void)
{
    return PLATFORM_OK;
}

/**
 * @brief 推进在飞操作：BUSY 若干次后给出结果；Abort 之后再 BUSY 若干次，最后报错。
 * @return 操作状态。
 */
Platform_StatusTypeDef Platform_Flash_ProcessOperation(void)
{
    fake_flash.process_calls++;
    if (fake_flash.aborted)
    {
        fake_flash.process_calls_after_abort++;
    }
    if (!fake_flash.active)
    {
        return PLATFORM_OK;
    }
    if (fake_flash.aborted)
    {
        if (fake_flash.abort_busy_polls > 0)
        {
            fake_flash.abort_busy_polls--;
            return PLATFORM_BUSY;
        }
        fake_flash.active = false;
        return PLATFORM_FLASH_ERROR;
    }
    if (fake_flash.polls_left != 0)
    {
        if (fake_flash.polls_left > 0)
        {
            fake_flash.polls_left--;
        }
        return PLATFORM_BUSY;
    }
    fake_flash.active = false;
    return fake_flash.final_status;
}

/**
 * @brief 在飞操作是否处于需要等待中断的硬件阶段（替身：有操作在飞即是）。
 * @return 有操作在飞为 true。
 */
bool Platform_Flash_OperationNeedsWait(void)
{
    return fake_flash.active;
}

/**
 * @brief 中止在飞操作：只记录调用，操作要等 Process 返回静止才算结束。
 */
void Platform_Flash_AbortOperation(void)
{
    fake_flash.abort_calls++;
    fake_flash.aborted = true;
}

/**
 * @brief 替“QSPI/MDMA 中断”向已订阅的回调发一个完成事件。
 */
static void flash_fire_irq(void)
{
    if ((fake_flash.callback != NULL) && fake_flash.active)
    {
        fake_flash.callback(PLATFORM_FLASH_OPERATION_EVENT_STATUS_MATCH, fake_flash.context);
    }
}

/* ------------------------------------------------------------------------- */
/* 公共脚手架                                                                 */
/* ------------------------------------------------------------------------- */

/**
 * @brief 阻塞钩子：等待将要发生时，替对应卷的“中断”按脚本发事件。
 * @param[in] wait 将要发生的等待。
 * @param[in,out] context 未使用。
 */
static void interrupt_on_wait(const FreertosStub_Wait *wait, void *context)
{
    (void)context;
    if (wait->kind != FREERTOS_STUB_WAIT_SEMAPHORE)
    {
        return;
    }
    if ((sd_done != NULL) && (wait->semaphore == sd_done))
    {
        sd_fire_scripted_irqs();
    }
    else if ((flash_done != NULL) && (wait->semaphore == flash_done) && fake_flash.irq_every_wait)
    {
        flash_fire_irq();
    }
}

/**
 * @brief 每个用例开头：重置桩与替身的脚本和计数，保留执行器内部已创建的信号量。
 */
static void begin_test(void)
{
    freertos_stub_reset();
    freertos_stub_set_block_hook(interrupt_on_wait, NULL);
    lock_env_counter_reset();
    lock_env_log_reset();
    memset(&fake_sd.start_status, 0, sizeof(fake_sd) - offsetof(FakeSd, start_status));
    fake_sd.complete_status = PLATFORM_OK;
    fake_flash.start_status = PLATFORM_OK;
    fake_flash.busy_polls = 0;
    fake_flash.abort_busy_polls = 0;
    fake_flash.final_status = PLATFORM_OK;
    fake_flash.irq_every_wait = true;
    fake_flash.starts = 0U;
    fake_flash.process_calls = 0U;
    fake_flash.process_calls_after_abort = 0U;
    fake_flash.abort_calls = 0U;
}

/**
 * @brief 取信号量调用统计的快照，用来算本用例内的增量（执行器的信号量跨用例存在）。
 * @param[in] semaphore 信号量句柄。
 * @return 统计快照。
 */
static FreertosStub_SemaphoreStats snapshot(SemaphoreHandle_t semaphore)
{
    return freertos_stub_semaphore_stats(semaphore);
}

/* ------------------------------------------------------------------------- */
/* SD 用例                                                                    */
/* ------------------------------------------------------------------------- */

static uint8_t sd_buffer[SD_MAX_TEST_BLOCKS * SD_BLOCK_BYTES];

/**
 * @brief 脚本化 SD 中断：每次等待发出给定的一串事件。
 * @param[in] events 事件数组。
 * @param[in] count 事件个数，1 至 SD_MAX_EVENTS。
 * @param[in] every_wait true 表示每次等待都发。
 */
static void sd_script_irq(const Platform_SD_TransferEventTypeDef *events, uint32_t count, bool every_wait)
{
    assert((count >= 1U) && (count <= SD_MAX_EVENTS));
    memcpy(fake_sd.irq_events, events, count * sizeof(events[0]));
    fake_sd.irq_event_count = count;
    fake_sd.irq_every_wait = every_wait;
}

/**
 * @brief 脚本化单个 SD 中断事件（只在第一次等待发出）。
 * @param[in] event 中断事件。
 */
static void sd_script_one(Platform_SD_TransferEventTypeDef event)
{
    sd_script_irq(&event, 1U, false);
}

/**
 * @brief 初始化之前不接受传输：没有信号量可等，也不启动 DMA。
 */
static void test_sd_transfer_refused_before_init(void)
{
    begin_test();
    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));
    assert(!filesystem_sd_transfer_write_blocks(sd_buffer, 0U, 1U, 100U));
    assert(fake_sd.start_calls == 0U);
    assert(fake_sd.complete_calls == 0U);
}

/**
 * @brief InitSD 先创建信号量再订阅传输事件；重复调用只重新订阅，不重建信号量、不丢信号。
 */
static void test_sd_init_creates_semaphore_before_subscribing(void)
{
    begin_test();
    assert(Service_Filesystem_InitSD() == SERVICE_OK);
    sd_done = freertos_stub_last_created_semaphore();
    assert(sd_done != NULL);
    assert(fake_sd.set_callback_calls == 1U);
    assert(fake_sd.semaphore_existed_when_callback_set);
    assert(xSemaphoreTake(sd_done, 0U) == pdFALSE);
    /* 钩子默认关闭：启动日志里没有 HOOK 行（DIAG-7：没有行就是没开）。 */
    assert(lock_env_log_count() == 0U);

    /* 一个已到达的信号，在再次 InitSD（挂载与卸载路径的幂等就绪检查）之后仍在。 */
    fake_sd.callback(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, fake_sd.context);
    assert(Service_Filesystem_InitSD() == SERVICE_OK);
    assert(fake_sd.set_callback_calls == 2U);
    assert(xSemaphoreTake(sd_done, 0U) == pdTRUE);
    assert(xSemaphoreTake(sd_done, 0U) == pdFALSE);
}

/**
 * @brief 读：中断给出读完成事件，等待成功，数据到达，事件提交给 Platform；中断里只用 FromISR。
 */
static void test_sd_read_completes_on_interrupt(void)
{
    FreertosStub_SemaphoreStats before;
    FreertosStub_SemaphoreStats after;

    begin_test();
    before = snapshot(sd_done);
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE);

    assert(filesystem_sd_transfer_read_blocks(sd_buffer, 10U, 2U, 100U));

    assert(sd_buffer[0] == 10U && sd_buffer[SD_BLOCK_BYTES - 1U] == 10U);
    assert(sd_buffer[SD_BLOCK_BYTES] == 11U && sd_buffer[2U * SD_BLOCK_BYTES - 1U] == 11U);
    assert(fake_sd.start_calls == 1U);
    assert(fake_sd.complete_calls == 1U);
    assert(fake_sd.completed[0] == PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE);
    after = snapshot(sd_done);
    assert(after.waits - before.waits == 1U);
    assert(after.give_from_isr_calls - before.give_from_isr_calls == 1U);
    assert(after.give_calls - before.give_calls == after.give_from_isr_calls - before.give_from_isr_calls);
    assert(xTaskGetTickCount() == 0U);
}

/**
 * @brief 回调把 GiveFromISR 的“唤醒了更高优先级任务”交给 portYIELD_FROM_ISR；没有唤醒就不请求让出。
 */
static void test_sd_interrupt_requests_yield_only_when_a_task_was_woken(void)
{
    begin_test();
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE);
    assert(filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));
    assert(freertos_stub_isr_yield_requests() == 0U);

    freertos_stub_set_give_from_isr_woken(pdTRUE);
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE);
    assert(filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));
    assert(freertos_stub_isr_yield_requests() == 1U);
}

/**
 * @brief 多个分块：每个分块各等一次完成，数据按块顺序拼起来。
 */
static void test_sd_read_in_chunks_waits_once_per_chunk(void)
{
    FreertosStub_SemaphoreStats before;
    uint32_t blocks = FILESYSTEM_SD_DMA_BLOCK_COUNT * 2U + 1U;
    Platform_SD_TransferEventTypeDef event = PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE;

    begin_test();
    before = snapshot(sd_done);
    sd_script_irq(&event, 1U, true);

    assert(filesystem_sd_transfer_read_blocks(sd_buffer, 0U, blocks, 100U));

    assert(fake_sd.start_calls == 3U);
    assert(fake_sd.complete_calls == 3U);
    assert(snapshot(sd_done).waits - before.waits == 3U);
    assert(sd_buffer[0] == 0U);
    assert(sd_buffer[(FILESYSTEM_SD_DMA_BLOCK_COUNT + 5U) * SD_BLOCK_BYTES] ==
           (uint8_t)(FILESYSTEM_SD_DMA_BLOCK_COUNT + 5U));
    assert(sd_buffer[(blocks - 1U) * SD_BLOCK_BYTES] == (uint8_t)(blocks - 1U));
}

/**
 * @brief 写：中断给出写完成事件，等待成功，数据按块交给 Platform。
 */
static void test_sd_write_completes_on_interrupt(void)
{
    begin_test();
    for (uint32_t block = 0U; block < 3U; block++)
    {
        memset(&sd_buffer[block * SD_BLOCK_BYTES], (int)(0x40U + block), SD_BLOCK_BYTES);
    }
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE);

    assert(filesystem_sd_transfer_write_blocks(sd_buffer, 7U, 3U, 100U));

    assert(fake_sd.written_blocks == 3U);
    assert(fake_sd.written[0] == 0x40U && fake_sd.written[1] == 0x41U && fake_sd.written[2] == 0x42U);
    assert(fake_sd.completed[0] == PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE);
}

/**
 * @brief 超时：没有中断，等满本分块预算，先向 Platform 提交显式中止（不再提交 ERROR），然后返回失败。
 *        中止停住了，所以 Device 回到可用状态，不留信号。
 */
static void test_sd_timeout_aborts_the_transfer_before_failing(void)
{
    begin_test();
    fake_sd.abort_us = 1500U;

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 7U, 1U, 100U));

    assert(strcmp(fake_sd.sequence, "SA") == 0);
    assert(fake_sd.abort_calls == 1U);
    assert(fake_sd.complete_calls == 0U);
    assert(xTaskGetTickCount() == pdMS_TO_TICKS(100U));
    assert(xSemaphoreTake(sd_done, 0U) == pdFALSE);
}

/**
 * @brief 中止的结果和耗时记一行 WARN（标签 FSABORT）：结果名、微秒、方向、起始块、块数；读和写都一样。
 */
static void test_sd_abort_result_and_elapsed_time_are_logged_as_one_warn_line(void)
{
    begin_test();
    fake_sd.abort_us = 1500U;

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 7U, 1U, 100U));

    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_tag(0U), "FSABORT") == 0);
    assert(strcmp(lock_env_log_text(0U), "stopped us=1500 R blk=7 n=1") == 0);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_WARN);

    begin_test();
    fake_sd.abort_us = 20U;
    assert(!filesystem_sd_transfer_write_blocks(sd_buffer, 9U, 3U, 100U));
    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_text(0U), "stopped us=20 W blk=9 n=3") == 0);
}

/**
 * @brief 周期计数器没有启动（频率为 0）时，耗时改按 tick 算，毫秒精度，而不是除以零。
 */
static void test_sd_abort_elapsed_time_falls_back_to_ticks_without_cycle_counter(void)
{
    begin_test();
    lock_env_set_counter_unavailable(true);
    fake_sd.abort_ticks = 3U;

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 5U, 2U, 100U));

    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_text(0U), "stopped us=3000 R blk=5 n=2") == 0);
}

/**
 * @brief 中止期间落进来的完成中断（中断在屏蔽之前就跑完了）留下的信号，在中止返回之后立刻清掉：
 *        关掉“清理之后、本次 Start 之前才完成”的窗口——硬件先停、信号量后清、下一笔才允许启动。
 *        紧接着再读也不被这个旧信号“完成”。
 */
static void test_sd_completion_that_lands_during_abort_is_cleared_after_it(void)
{
    begin_test();
    fake_sd.abort_fires_late_completion = true;

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));

    assert(xSemaphoreTake(sd_done, 0U) == pdFALSE);

    /* 紧接着再读：这次没有脚本化的中断，必须超时而不是被中止期间的旧信号“完成”。 */
    fake_sd.abort_fires_late_completion = false;
    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));
    assert(fake_sd.abort_calls == 2U);
    assert(fake_sd.complete_calls == 0U);
}

/**
 * @brief 到上界还没停住：记 stuck；卷保持 ERROR，之后的传输在启动处就被拒绝——不等待、不再中止、不提交事件。
 */
static void test_sd_abort_that_does_not_stop_is_reported_and_the_volume_stays_refused(void)
{
    FreertosStub_SemaphoreStats before;

    begin_test();
    fake_sd.abort_result = PLATFORM_SD_ABORT_STUCK;

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 11U, 4U, 100U));

    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_text(0U), "stuck us=0 R blk=11 n=4") == 0);
    assert(fake_sd.complete_calls == 0U);

    before = snapshot(sd_done);
    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 11U, 4U, 100U));
    assert(!filesystem_sd_transfer_write_blocks(sd_buffer, 11U, 4U, 100U));
    assert(strcmp(fake_sd.sequence, "SASS") == 0);
    assert(snapshot(sd_done).waits == before.waits);
    assert(lock_env_log_count() == 1U);
}

/**
 * @brief 中止时发现卡已拔出：记 no_media，不提交事件。
 */
static void test_sd_abort_with_card_removed_is_reported_as_no_media(void)
{
    begin_test();
    fake_sd.abort_result = PLATFORM_SD_ABORT_NO_MEDIA;

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 3U, 1U, 100U));

    assert(strcmp(lock_env_log_text(0U), "no_media us=0 R blk=3 n=1") == 0);
    assert(fake_sd.complete_calls == 0U);
}

/**
 * @brief Platform 没有受理中止（没有在飞的传输或 Port 不支持）：退回旧做法，向 Platform 提交 ERROR，
 *        让 Device 进入 ERROR；日志记 rejected。
 */
static void test_sd_rejected_abort_falls_back_to_completing_with_error(void)
{
    begin_test();
    fake_sd.abort_result = PLATFORM_SD_ABORT_REJECTED;

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 2U, 1U, 100U));

    assert(strcmp(fake_sd.sequence, "SAC") == 0);
    assert(fake_sd.completed[0] == PLATFORM_SD_TRANSFER_EVENT_ERROR);
    assert(strcmp(lock_env_log_text(0U), "rejected us=0 R blk=2 n=1") == 0);
}

/**
 * @brief 中止停住之后紧接着再读成功（R2.2 的“超时后紧接着再读并校验”）：数据正确，没有多余的事件。
 */
static void test_sd_read_right_after_a_stopped_abort_succeeds(void)
{
    begin_test();

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U)); /* 超时，中止停住 */
    assert(strcmp(fake_sd.sequence, "SA") == 0);

    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE);
    assert(filesystem_sd_transfer_read_blocks(sd_buffer, 21U, 2U, 100U));

    assert(strcmp(fake_sd.sequence, "SASC") == 0);
    assert(sd_buffer[0] == 21U && sd_buffer[SD_BLOCK_BYTES] == 22U);
    assert(fake_sd.abort_calls == 1U);
    assert(lock_env_log_count() == 1U);
}

/**
 * @brief 不是超时的失败（错误事件、方向不符、启动被拒）不触发中止：硬件已经自己收尾，或根本没启动。
 */
static void test_sd_failures_other_than_timeout_do_not_abort(void)
{
    begin_test();
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_ERROR);
    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));

    begin_test();
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE);
    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));

    begin_test();
    fake_sd.start_status = PLATFORM_BUSY;
    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));

    assert(fake_sd.abort_calls == 0U);
    assert(lock_env_log_count() == 0U);
}

/**
 * @brief 错误事件：等待成功但事件不是期望的完成，提交该事件并返回失败。
 */
static void test_sd_error_event_fails_the_transfer(void)
{
    begin_test();
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_ERROR);

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));

    assert(fake_sd.complete_calls == 1U);
    assert(fake_sd.completed[0] == PLATFORM_SD_TRANSFER_EVENT_ERROR);
    assert(xTaskGetTickCount() == 0U);
}

/**
 * @brief 方向不符的完成事件算失败，实际事件仍提交给 Platform。
 */
static void test_sd_wrong_direction_event_fails_the_transfer(void)
{
    begin_test();
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE);

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));

    assert(fake_sd.completed[0] == PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE);
}

/**
 * @brief 同一次等待里先后到两个事件，最后一个有效（与原先的覆盖式通知一致）。
 */
static void test_sd_last_interrupt_event_wins(void)
{
    const Platform_SD_TransferEventTypeDef events[] = {PLATFORM_SD_TRANSFER_EVENT_ERROR,
                                                       PLATFORM_SD_TRANSFER_EVENT_ABORTED};

    begin_test();
    sd_script_irq(events, 2U, false);

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));

    assert(fake_sd.complete_calls == 1U);
    assert(fake_sd.completed[0] == PLATFORM_SD_TRANSFER_EVENT_ABORTED);
}

/**
 * @brief 启动被 Platform 拒绝：不等待、不提交事件。
 */
static void test_sd_start_refused_does_not_wait(void)
{
    FreertosStub_SemaphoreStats before;

    begin_test();
    before = snapshot(sd_done);
    fake_sd.start_status = PLATFORM_BUSY;

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));

    assert(fake_sd.start_calls == 1U);
    assert(fake_sd.complete_calls == 0U);
    assert(snapshot(sd_done).waits == before.waits);
}

/**
 * @brief 迟到的旧信号：上一次超时之后才到的完成中断，不会被下一次传输当成它自己的完成。
 */
static void test_sd_late_signal_from_a_timed_out_transfer_is_discarded(void)
{
    begin_test();

    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U)); /* 超时，中止停住 */
    assert(fake_sd.abort_calls == 1U);

    /* 超时之后，旧 DMA 才完成：信号量被置位，没有任何任务在等。 */
    fake_sd.callback(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, fake_sd.context);

    /* 下一次传输没有新的中断：必须仍然超时，而不是被旧信号“完成”。 */
    assert(!filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));
    assert(fake_sd.abort_calls == 2U);
    assert(fake_sd.complete_calls == 0U);

    /* 再下一次有真正的中断：成功，且没有多余信号留下。 */
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE);
    assert(filesystem_sd_transfer_read_blocks(sd_buffer, 0U, 1U, 100U));
    assert(fake_sd.completed[0] == PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE);
    assert(xSemaphoreTake(sd_done, 0U) == pdFALSE);
}

/**
 * @brief 传输不看调用任务是谁：换一个任务照常完成。
 */
static void test_sd_transfer_does_not_depend_on_calling_task(void)
{
    TaskHandle_t other;

    begin_test();
    other = freertos_stub_task_create();
    assert(other != NULL);
    freertos_stub_set_current_task(other);
    sd_script_one(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE);

    assert(filesystem_sd_transfer_read_blocks(sd_buffer, 3U, 1U, 100U));

    assert(sd_buffer[0] == 3U);
    assert(fake_sd.completed[0] == PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE);
}

/* ------------------------------------------------------------------------- */
/* Flash 用例                                                                 */
/* ------------------------------------------------------------------------- */

static uint8_t flash_buffer[4U * FLASH_SECTOR_BYTES];

/**
 * @brief 初始化之前 Flash 诊断入口不启动任何操作，磁盘状态也不报就绪。
 */
static void test_flash_entries_refused_before_init(void)
{
    begin_test();
    assert(Service_Filesystem_ReadFlashArray(0U, flash_buffer, 64U, 100U) == SERVICE_NOT_READY);
    assert(Service_Filesystem_EraseFlashDiagnostic(PLATFORM_FLASH_DIAGNOSTIC_REGION_HEAD, 100U) ==
           SERVICE_NOT_READY);
    assert(fake_flash.starts == 0U);
    assert(BSP_USER_DISKIO_GetStatus(0) == STA_NOINIT);
}

/**
 * @brief InitFlash 先创建信号量再订阅；重复调用既不重建信号量，也不丢信号。
 */
static void test_flash_init_creates_semaphore_before_subscribing(void)
{
    begin_test();
    assert(Service_Filesystem_InitFlash() == SERVICE_OK);
    flash_done = freertos_stub_last_created_semaphore();
    assert(flash_done != NULL);
    assert(flash_done != sd_done);
    assert(fake_flash.set_callback_calls == 1U);
    assert(fake_flash.semaphore_existed_when_callback_set);
    assert(xSemaphoreTake(flash_done, 0U) == pdFALSE);

    fake_flash.callback(PLATFORM_FLASH_OPERATION_EVENT_STATUS_MATCH, fake_flash.context);
    assert(Service_Filesystem_InitFlash() == SERVICE_OK);
    assert(fake_flash.set_callback_calls == 1U);
    assert(xSemaphoreTake(flash_done, 0U) == pdTRUE);
    assert(xSemaphoreTake(flash_done, 0U) == pdFALSE);
}

/**
 * @brief 硬件阶段等待：每次等待由中断给出一次信号，Process 看到完成才返回成功。
 */
static void test_flash_operation_completes_on_interrupt(void)
{
    FreertosStub_SemaphoreStats before;
    FreertosStub_SemaphoreStats after;

    begin_test();
    before = snapshot(flash_done);
    fake_flash.busy_polls = 2;

    assert(Service_Filesystem_ReadFlashArray(0U, flash_buffer, 64U, 100U) == SERVICE_OK);

    assert(flash_buffer[0] == 0xA5U);
    assert(fake_flash.process_calls == 3U);
    after = snapshot(flash_done);
    assert(after.waits - before.waits == 2U);
    assert(after.give_from_isr_calls - before.give_from_isr_calls == 2U);
    assert(after.give_calls - before.give_calls == after.give_from_isr_calls - before.give_from_isr_calls);
    assert(freertos_stub_isr_yield_requests() == 0U);
}

/**
 * @brief 回调把 GiveFromISR 的“唤醒了更高优先级任务”交给 portYIELD_FROM_ISR。
 */
static void test_flash_interrupt_requests_yield_when_a_task_was_woken(void)
{
    begin_test();
    fake_flash.busy_polls = 1;
    freertos_stub_set_give_from_isr_woken(pdTRUE);

    assert(Service_Filesystem_ReadFlashArray(0U, flash_buffer, 64U, 100U) == SERVICE_OK);

    assert(freertos_stub_isr_yield_requests() == 1U);
}

/**
 * @brief 操作一开始就完成：不需要等待，也就不碰信号量的等待路径。
 */
static void test_flash_operation_finished_at_once_does_not_wait(void)
{
    FreertosStub_SemaphoreStats before;

    begin_test();
    before = snapshot(flash_done);

    assert(Service_Filesystem_ReadFlashArray(0U, flash_buffer, 64U, 100U) == SERVICE_OK);

    assert(snapshot(flash_done).waits == before.waits);
    assert(fake_flash.process_calls == 1U);
}

/**
 * @brief 迟到的旧信号：上一次操作遗留的信号不会让下一次操作的第一次等待白白过去。
 */
static void test_flash_stale_signal_is_discarded_before_waiting(void)
{
    FreertosStub_SemaphoreStats before;

    begin_test();
    before = snapshot(flash_done);
    fake_flash.callback(PLATFORM_FLASH_OPERATION_EVENT_STATUS_MATCH, fake_flash.context); /* 无人等待时到达 */
    fake_flash.busy_polls = 1;
    fake_flash.irq_every_wait = false; /* 这次操作的完成中断一直不来，靠短周期重查看到完成 */

    assert(Service_Filesystem_ReadFlashArray(0U, flash_buffer, 64U, 100U) == SERVICE_OK);

    assert(snapshot(flash_done).waits - before.waits == 1U); /* 这次等待是真的等了一个周期 */
    assert(xTaskGetTickCount() == pdMS_TO_TICKS(FILESYSTEM_FLASH_WAIT_SLICE_MS));
    assert(xSemaphoreTake(flash_done, 0U) == pdFALSE);
}

/**
 * @brief 超时：预算到期先 Abort，然后继续 Process 直到控制器静止，最后报超时。
 */
static void test_flash_timeout_aborts_and_keeps_processing_until_quiet(void)
{
    begin_test();
    fake_flash.busy_polls = -1; /* 自己永远不会完成 */
    fake_flash.abort_busy_polls = 2;
    fake_flash.irq_every_wait = false;

    assert(Service_Filesystem_ReadFlashArray(0U, flash_buffer, 64U, 10U) == SERVICE_TIMEOUT);

    assert(fake_flash.abort_calls == 1U);
    assert(fake_flash.process_calls_after_abort == 3U); /* BUSY、BUSY、静止 */
    assert(!fake_flash.active);
    assert(xTaskGetTickCount() >= pdMS_TO_TICKS(10U));
}

/**
 * @brief 错误与拒绝：Process 报错得 ERROR；Start 被拒绝得 BUSY 或 ERROR，且不等待。
 */
static void test_flash_error_and_refused_start_are_reported(void)
{
    FreertosStub_SemaphoreStats before;

    begin_test();
    fake_flash.busy_polls = 1;
    fake_flash.final_status = PLATFORM_FLASH_ERROR;
    assert(Service_Filesystem_ReadFlashArray(0U, flash_buffer, 64U, 100U) == SERVICE_ERROR);
    assert(fake_flash.abort_calls == 0U);

    begin_test();
    before = snapshot(flash_done);
    fake_flash.start_status = PLATFORM_BUSY;
    assert(Service_Filesystem_ReadFlashArray(0U, flash_buffer, 64U, 100U) == SERVICE_BUSY);
    fake_flash.start_status = PLATFORM_FLASH_ERROR;
    assert(Service_Filesystem_ReadFlashArray(0U, flash_buffer, 64U, 100U) == SERVICE_ERROR);
    assert(fake_flash.process_calls == 0U);
    assert(snapshot(flash_done).waits == before.waits);
}

/**
 * @brief 磁盘状态只看卷是否就绪，不看调用任务：初始化它的任务和别的任务读到同一个答案。
 */
static void test_flash_disk_status_does_not_depend_on_calling_task(void)
{
    TaskHandle_t initializer;
    TaskHandle_t other;

    begin_test();
    initializer = xTaskGetCurrentTaskHandle();
    other = freertos_stub_task_create();
    assert(other != NULL && other != initializer);

    fake_flash.volume_state = PLATFORM_FLASH_VOLUME_READY;
    assert(BSP_USER_DISKIO_GetStatus(0) == 0);
    freertos_stub_set_current_task(other);
    assert(BSP_USER_DISKIO_GetStatus(0) == 0);
    assert(BSP_USER_DISKIO_Init(0) == 0);

    fake_flash.volume_state = PLATFORM_FLASH_VOLUME_RESET;
    assert(BSP_USER_DISKIO_GetStatus(0) == STA_NOINIT);
    fake_flash.volume_state = PLATFORM_FLASH_VOLUME_READY;
}

/**
 * @brief 磁盘读写与同步经完成等待：成功、错误和超时；超时的操作先中止、静止后才返回。
 */
static void test_flash_disk_io_waits_for_completion(void)
{
    TaskHandle_t other;
    uint8_t pattern[FLASH_SECTOR_BYTES];

    begin_test();
    other = freertos_stub_task_create();
    freertos_stub_set_current_task(other);
    fake_flash.volume_state = PLATFORM_FLASH_VOLUME_READY;
    memset(pattern, 0x3C, sizeof(pattern));
    fake_flash.busy_polls = 1;

    assert(BSP_USER_DISKIO_WriteBlocks(0, pattern, 5U, 1U) == RES_OK);
    assert(BSP_USER_DISKIO_ReadBlocks(0, flash_buffer, 5U, 1U) == RES_OK);
    assert(memcmp(flash_buffer, pattern, sizeof(pattern)) == 0);
    assert(BSP_USER_DISKIO_Ioctl(0, CTRL_SYNC, NULL) == RES_OK);

    fake_flash.final_status = PLATFORM_FLASH_ERROR;
    assert(BSP_USER_DISKIO_WriteBlocks(0, pattern, 6U, 1U) == RES_ERROR);
    assert(BSP_USER_DISKIO_Ioctl(0, CTRL_SYNC, NULL) == RES_ERROR);

    fake_flash.final_status = PLATFORM_OK;
    fake_flash.busy_polls = -1;
    fake_flash.irq_every_wait = false;
    assert(BSP_USER_DISKIO_ReadBlocks(0, flash_buffer, 5U, 1U) == RES_ERROR);
    assert(fake_flash.abort_calls == 1U);
    assert(!fake_flash.active);
    assert(xTaskGetTickCount() >= pdMS_TO_TICKS(FILESYSTEM_FLASH_READ_TIMEOUT_MS));
}

/**
 * @brief 真实 FatFs 与 Service：别的任务格式化、挂载并读写 Flash 卷，每次磁盘访问都经完成等待。
 *        旧实现里非所有者的磁盘状态是 STA_NOINIT，FatFs 会因此丢弃挂载状态。
 */
static void test_flash_volume_works_from_a_task_that_did_not_initialize_it(void)
{
    TaskHandle_t other;
    Service_Filesystem_FileHandleTypeDef file = {0};
    const uint8_t payload[] = {9, 8, 7, 6, 5};
    uint8_t readback[sizeof(payload)] = {0};
    uint32_t transferred = 0U;
    FreertosStub_SemaphoreStats before;

    begin_test();
    other = freertos_stub_task_create();
    freertos_stub_set_current_task(other);
    fake_flash.busy_polls = 1;
    before = snapshot(flash_done);

    assert(Service_Filesystem_FormatAndMountFlash() == SERVICE_OK);
    assert(BSP_USER_DISKIO_GetStatus(0) == 0);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "other.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_CREATE_NEW,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_WriteFile(file, payload, sizeof(payload), &transferred) == SERVICE_OK);
    assert(transferred == sizeof(payload));
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);
    assert(Service_Filesystem_OpenFile(SERVICE_FILESYSTEM_VOLUME_FLASH,
                                       "other.bin",
                                       SERVICE_FILESYSTEM_FILE_MODE_READ,
                                       &file) == SERVICE_OK);
    assert(Service_Filesystem_ReadFile(file, readback, sizeof(readback), &transferred) == SERVICE_OK);
    assert(transferred == sizeof(readback) && memcmp(readback, payload, sizeof(payload)) == 0);
    assert(Service_Filesystem_CloseFile(file) == SERVICE_OK);

    assert(snapshot(flash_done).waits - before.waits > 4U); /* 格式化与读写确实走了等待路径 */
    assert(fake_flash.abort_calls == 0U);
}

/**
 * @brief 运行全部用例。
 * @return 断言全部通过返回 0。
 */

/* ------------------------------------------------------------------------- */
/* 迟到 DMA 测试钩子（FILESYSTEM_HOOK_SD_LATE_DMA=1 的构建）                  */
/* ------------------------------------------------------------------------- */

#if FILESYSTEM_HOOK_SD_LATE_DMA

/**
 * @brief 开始一个钩子用例：重置脚本，做 PERIOD-1 次普通读，钩子的计数停在“下一次读就触发探测”。
 *        每个用例恰好消耗 PERIOD 次读请求，所以钩子的计数在用例之间保持对齐。
 */
static void hook_begin_period(void)
{
    Platform_SD_TransferEventTypeDef ok = PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE;

    begin_test();
    sd_script_irq(&ok, 1U, true);
    for (uint32_t index = 0U; index + 1U < FILESYSTEM_HOOK_SD_LATE_DMA_PERIOD; index++)
    {
        assert(filesystem_sd_transfer_read_blocks(sd_buffer, 500U, 1U, 100U));
    }
    assert(lock_env_log_count() == 0U);
    assert(fake_sd.start_calls == FILESYSTEM_HOOK_SD_LATE_DMA_PERIOD - 1U);
    fake_sd.sequence[0] = '\0';
    fake_sd.irq_event_count = 0U;
}

/**
 * @brief 做第 PERIOD 次读请求（触发探测的那一次），中断事件按等待次序由计划给出。
 * @param[in] probe_start_block 这次读的起始块（探测的目标块）。
 * @param[in] plan 第 PERIOD 次读（含探测里的三次读）按等待次序给的中断事件；NONE 表示这次等待超时。
 * @param[in] plan_len 计划项数。
 * @return 调用者这次读的结果。
 */
static bool hook_run_probe_read(uint32_t probe_start_block,
                                const Platform_SD_TransferEventTypeDef *plan,
                                uint32_t plan_len)
{
    fake_sd.wait_plan_len = plan_len;
    fake_sd.wait_index = 0U;
    memcpy(fake_sd.wait_plan, plan, plan_len * sizeof(plan[0]));
    return filesystem_sd_transfer_read_blocks(sd_buffer, probe_start_block, 2U, 100U);
}

/**
 * @brief 钩子打开时，InitSD 在启动日志里投一行 HOOK，说明开了什么；重复 InitSD 不重复投。
 */
static void test_hook_announces_itself_once_in_the_boot_log(void)
{
    begin_test();
    assert(Service_Filesystem_InitSD() == SERVICE_OK);
    sd_done = freertos_stub_last_created_semaphore();
    assert(Service_Filesystem_InitSD() == SERVICE_OK);

    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_tag(0U), "HOOK") == 0);
    assert(strcmp(lock_env_log_text(0U), "late_dma ON period=16 timeout=1ms") == 0);
}

/**
 * @brief 每 PERIOD 次读请求探测一次：先读目标块、再让一笔读超时（单块超时 1 ms）并被中止、紧接着再读目标块并校验内容
 *        一致，然后调用者自己的读照常完成。中止的 WARN 行照常出现，探测结果再记一行 INFO。
 */
static void test_hook_probes_every_period_and_verifies_the_read_after_the_abort(void)
{
    const Platform_SD_TransferEventTypeDef plan[] = {
        PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, PLATFORM_SD_TRANSFER_EVENT_NONE,
        PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE};

    hook_begin_period();

    assert(hook_run_probe_read(100U, plan, 4U));

    assert(strcmp(fake_sd.sequence, "SCSASCSC") == 0);
    assert(sd_buffer[0] == 100U && sd_buffer[SD_BLOCK_BYTES] == 101U);
    assert(lock_env_log_count() == 2U);
    assert(strcmp(lock_env_log_tag(0U), "FSABORT") == 0);
    assert(strcmp(lock_env_log_text(0U), "stopped us=0 R blk=0 n=64") == 0);
    assert(strcmp(lock_env_log_tag(1U), "SDHOOK") == 0);
    assert(strcmp(lock_env_log_text(1U), "late_dma n=1 blk=100 victim=timeout verify=ok") == 0);
    assert(lock_env_log_level(1U) == (uint32_t)SERVICE_LOG_LEVEL_INFO);
    /* 受害读只等 1 ms，其余读没有超时。 */
    assert(xTaskGetTickCount() == pdMS_TO_TICKS(FILESYSTEM_HOOK_SD_LATE_DMA_TIMEOUT_MS));
}

/**
 * @brief 迟到的旧 DMA 把缓冲写坏的话，探测里的第二次读与第一次不一致：记 MISMATCH，等级 ERROR。
 */
static void test_hook_reports_a_mismatch_when_a_late_write_corrupts_the_next_read(void)
{
    const Platform_SD_TransferEventTypeDef plan[] = {
        PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, PLATFORM_SD_TRANSFER_EVENT_NONE,
        PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE};

    hook_begin_period();
    fake_sd.corrupt_read_after_abort = true;

    assert(hook_run_probe_read(100U, plan, 4U));

    assert(lock_env_log_count() == 2U);
    assert(strcmp(lock_env_log_text(1U), "late_dma n=2 blk=100 victim=timeout verify=MISMATCH") == 0);
    assert(lock_env_log_level(1U) == (uint32_t)SERVICE_LOG_LEVEL_ERROR);
}

/**
 * @brief 中止没有停住（stuck）：卷保持 ERROR，探测里紧接着的那次读被拒绝，记 read_failed（ERROR）；调用者自己的读也失败。
 */
static void test_hook_reports_read_failed_when_the_abort_does_not_stop(void)
{
    const Platform_SD_TransferEventTypeDef plan[] = {
        PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, PLATFORM_SD_TRANSFER_EVENT_NONE};

    hook_begin_period();
    fake_sd.abort_result = PLATFORM_SD_ABORT_STUCK;

    assert(!hook_run_probe_read(100U, plan, 2U));

    assert(strcmp(fake_sd.sequence, "SCSASS") == 0);
    assert(lock_env_log_count() == 2U);
    assert(strcmp(lock_env_log_text(0U), "stuck us=0 R blk=0 n=64") == 0);
    assert(strcmp(lock_env_log_text(1U), "late_dma n=3 blk=100 victim=timeout verify=read_failed") == 0);
    assert(lock_env_log_level(1U) == (uint32_t)SERVICE_LOG_LEVEL_ERROR);
}

/**
 * @brief 目标块落在卷头部（引导区一带，与受害块内容可能相同）时不探测：不多读、不记日志。
 */
static void test_hook_skips_reads_near_the_start_of_the_volume(void)
{
    const Platform_SD_TransferEventTypeDef plan[] = {PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE};

    hook_begin_period();

    assert(hook_run_probe_read(10U, plan, 1U));

    assert(strcmp(fake_sd.sequence, "SC") == 0);
    assert(lock_env_log_count() == 0U);
}

/**
 * @brief 受害读在 1 ms 内居然完成了（卡很快）：记 victim=done，说明这次没有制造出迟到的 DMA，不算失败。
 */
static void test_hook_reports_when_the_victim_read_did_not_time_out(void)
{
    const Platform_SD_TransferEventTypeDef plan[] = {
        PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE,
        PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE, PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE};

    hook_begin_period();

    assert(hook_run_probe_read(100U, plan, 4U));

    assert(fake_sd.abort_calls == 0U);
    assert(lock_env_log_count() == 1U);
    assert(strcmp(lock_env_log_text(0U), "late_dma n=4 blk=100 victim=done verify=ok") == 0);
    assert(lock_env_log_level(0U) == (uint32_t)SERVICE_LOG_LEVEL_INFO);
}

/**
 * @brief 探测里第一次读（读目标块）就失败：不做受害读和校验，记 skipped（WARN）；失败本身已由中止的那一行说明。
 */
static void test_hook_skips_verification_when_the_first_probe_read_fails(void)
{
    const Platform_SD_TransferEventTypeDef plan[] = {PLATFORM_SD_TRANSFER_EVENT_NONE};

    hook_begin_period();

    assert(!hook_run_probe_read(100U, plan, 1U));

    assert(lock_env_log_count() >= 2U);
    assert(strcmp(lock_env_log_text(0U), "stopped us=0 R blk=100 n=2") == 0);
    assert(strcmp(lock_env_log_text(1U), "late_dma n=5 blk=100 victim=none verify=skipped") == 0);
    assert(lock_env_log_level(1U) == (uint32_t)SERVICE_LOG_LEVEL_WARN);
}

/**
 * @brief 写请求从不触发探测，也不计入读请求的节拍。
 */
static void test_hook_never_probes_writes(void)
{
    const Platform_SD_TransferEventTypeDef done = PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE;

    begin_test();
    sd_script_irq(&done, 1U, true);
    for (uint32_t index = 0U; index < FILESYSTEM_HOOK_SD_LATE_DMA_PERIOD * 2U; index++)
    {
        assert(filesystem_sd_transfer_write_blocks(sd_buffer, 500U, 1U, 100U));
    }
    assert(fake_sd.abort_calls == 0U);
    assert(lock_env_log_count() == 0U);
}

#endif /* FILESYSTEM_HOOK_SD_LATE_DMA */

/**
 * @brief 依次运行用例：默认构建跑 SD 与 Flash 完成等待用例；钩子构建只跑迟到 DMA 钩子用例。
 * @return 全部通过时为 0，断言失败时进程中止。
 */
int main(void)
{
    strcpy(SDPath, "1:/");
    retSD = 0U;
    retUSER = FATFS_LinkDriver(&USER_Driver, USERPath);
    assert(retUSER == 0U);

#if FILESYSTEM_HOOK_SD_LATE_DMA
    /* 钩子构建：只跑钩子用例（别的用例的读次数会被钩子的节拍打乱）。 */
    test_hook_announces_itself_once_in_the_boot_log();
    test_hook_probes_every_period_and_verifies_the_read_after_the_abort();
    test_hook_reports_a_mismatch_when_a_late_write_corrupts_the_next_read();
    test_hook_reports_read_failed_when_the_abort_does_not_stop();
    test_hook_skips_reads_near_the_start_of_the_volume();
    test_hook_reports_when_the_victim_read_did_not_time_out();
    test_hook_skips_verification_when_the_first_probe_read_fails();
    test_hook_never_probes_writes();
    puts("filesystem_completion: SD late-DMA hook tests passed");
    return 0;
#endif

    test_sd_transfer_refused_before_init();
    test_sd_init_creates_semaphore_before_subscribing();
    test_sd_read_completes_on_interrupt();
    test_sd_interrupt_requests_yield_only_when_a_task_was_woken();
    test_sd_read_in_chunks_waits_once_per_chunk();
    test_sd_write_completes_on_interrupt();
    test_sd_timeout_aborts_the_transfer_before_failing();
    test_sd_abort_result_and_elapsed_time_are_logged_as_one_warn_line();
    test_sd_abort_elapsed_time_falls_back_to_ticks_without_cycle_counter();
    test_sd_completion_that_lands_during_abort_is_cleared_after_it();
    test_sd_abort_that_does_not_stop_is_reported_and_the_volume_stays_refused();
    test_sd_abort_with_card_removed_is_reported_as_no_media();
    test_sd_rejected_abort_falls_back_to_completing_with_error();
    test_sd_read_right_after_a_stopped_abort_succeeds();
    test_sd_failures_other_than_timeout_do_not_abort();
    test_sd_error_event_fails_the_transfer();
    test_sd_wrong_direction_event_fails_the_transfer();
    test_sd_last_interrupt_event_wins();
    test_sd_start_refused_does_not_wait();
    test_sd_late_signal_from_a_timed_out_transfer_is_discarded();
    test_sd_transfer_does_not_depend_on_calling_task();
    puts("filesystem_completion: SD completion tests passed");

    test_flash_entries_refused_before_init();
    test_flash_init_creates_semaphore_before_subscribing();
    test_flash_operation_completes_on_interrupt();
    test_flash_interrupt_requests_yield_when_a_task_was_woken();
    test_flash_operation_finished_at_once_does_not_wait();
    test_flash_stale_signal_is_discarded_before_waiting();
    test_flash_timeout_aborts_and_keeps_processing_until_quiet();
    test_flash_error_and_refused_start_are_reported();
    test_flash_disk_status_does_not_depend_on_calling_task();
    test_flash_disk_io_waits_for_completion();
    test_flash_volume_works_from_a_task_that_did_not_initialize_it();
    puts("filesystem_completion: Flash completion tests passed");
    return 0;
}
