/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  CubeMX FatFs application seam and SD DMA DiskIO bridge.
  *
  * @details
  *          CubeMX owns the FatFs driver-link variables below. The BSP_SD_*()
  *          definitions remain in this USER CODE area because they are the
  *          generated middleware's supported override seam. Their behaviour is
  *          nevertheless owned by the Storage Task: each call starts Platform
  *          SD DMA, waits for that task's indexed notification, then reports a
  *          synchronous result to FatFs.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "fatfs.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FATFS/Target/bsp_driver_sd.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"
#include "Platform/sd/platform_sd.h"
#include "stm32h7xx_hal.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */
uint8_t retUSER;  /* Return value for USER */
char USERPath[4]; /* USER logical drive path */
FATFS USERFatFS;  /* File system object for USER logical drive */
FIL USERFile;     /* File object for USER */

/* USER CODE BEGIN Variables */

/** @brief 一个 FatFs 逻辑扇区与 SD 卡逻辑块的固定字节数。 */
#define FATFS_SD_BLOCK_SIZE                  512U
/** @brief 单次 DMA 最多搬运的扇区数；同时也是中转缓冲区的串行窗口。 */
#define FATFS_SD_DMA_BLOCK_COUNT             8U
/** @brief Cortex-M7 D-Cache 的 Cache line 大小。 */
#define FATFS_SD_DMA_CACHE_LINE_SIZE         32U
/** @brief BSP DMA 入口没有 timeout 参数时使用的最大任务等待时间。 */
#define FATFS_SD_DMA_TIMEOUT_MS              30000U

#if ((FATFS_SD_BLOCK_SIZE % FATFS_SD_DMA_CACHE_LINE_SIZE) != 0U)
#error "SD logical block size must cover an integral number of D-Cache lines."
#endif

/**
  * @brief FatFs 卷操作所属的唯一普通任务。
  * @details
  *          当前架构规定 Storage Task 独占 FatFs 和 SD 生命周期。记录其 Handle 后，
  *          本桥接层能拒绝其他任务等待或消费 Storage 的 DMA 完成通知。
  */
static TaskHandle_t hfatfs_sd_owner_task;

/**
  * @brief SDMMC DMA 的专用、中转、Cache-line 对齐缓冲区。
  * @details
  *          默认 .bss 位于 DTCM；SDMMC 外设不能安全地把 DTCM 用作 DMA 目标。链接
  *          脚本将 .sd_dma_buffer 放到 AXI SRAM。CPU 与 DMA 又不硬件一致：CPU 的
  *          最新数据可能还在 D-Cache，DMA 写入 RAM 后 CPU 也可能继续读取旧 Cache。
  *          32 字节对齐和 512 字节整数倍长度确保 Cache 操作不会覆盖相邻变量。
  */
static uint8_t fatfs_sd_dma_buffer[FATFS_SD_BLOCK_SIZE * FATFS_SD_DMA_BLOCK_COUNT]
    __attribute__((aligned(FATFS_SD_DMA_CACHE_LINE_SIZE), section(".sd_dma_buffer")));

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);
  /*## FatFS: Link the USER driver ###########################*/
  retUSER = FATFS_LinkDriver(&USER_Driver, USERPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/* USER CODE BEGIN Application */

/**
  * @brief  绑定首次进入 Filesystem Service 的 Storage Task。
  * @retval true 当前任务已成为或已经是唯一卷操作拥有者。
  * @retval false 调度器尚未运行，或另一任务已经取得所有权。
  * @note   该函数由 Filesystem_Init() 调用，不是给普通业务任务直接使用的 API。
  */
bool FatFs_SD_BindCurrentTask(void)
{
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();

    if (current_task == NULL)
    {
        return false;
    }

    if (hfatfs_sd_owner_task == NULL)
    {
        hfatfs_sd_owner_task = current_task;
    }

    return hfatfs_sd_owner_task == current_task;
}

/**
  * @brief  在 SDMMC DMA 向中转缓冲区写入前建立接收方向的一致性。
  * @param  byte_count 本次 DMA 将覆盖的字节数，必须为 32 的整数倍。
  * @details
  *          Clean 先把同一 Cache line 中可能残留的脏数据写回 RAM；若跳过这一步，
  *          一条旧的脏 line 可能在 DMA 进行期间被替换并写回，覆盖 DMA 刚写入的数据。
  *          随后 Invalidate 丢弃 CPU 的旧副本，确保 DMA 结束后 CPU 必须从 RAM 取数。
  */
static void fatfs_sd_dma_prepare_read(uint32_t byte_count)
{
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)fatfs_sd_dma_buffer,
                                      (int32_t)byte_count);
    __DSB();
}

/**
  * @brief  在 SDMMC DMA 读取中转缓冲区前建立发送方向的一致性。
  * @param  byte_count 本次 DMA 将读取的字节数，必须为 32 的整数倍。
  * @details CPU 已通过 memcpy() 更新了 Cache 中的中转数据。Clean 把这些脏 line
  *          写回 AXI SRAM；否则 DMA 只能看到旧 RAM 内容。写方向完成后无需 Invalidate，
  *          因为 DMA 没有修改该缓冲区。
  */
static void fatfs_sd_dma_prepare_write(uint32_t byte_count)
{
    SCB_CleanDCache_by_Addr((uint32_t *)fatfs_sd_dma_buffer, (int32_t)byte_count);
    __DSB();
}

/**
  * @brief  在 DMA 接收完成后使 CPU 重新从 RAM 读取数据。
  * @param  byte_count 本次 DMA 覆盖的字节数，必须为 32 的整数倍。
  * @note   此函数必须在接收完成事件之后调用；提前 Invalidate 不会等待 DMA，CPU
  *         仍可能在外设尚未写完时读到不完整的新数据。
  */
static void fatfs_sd_dma_finish_read(uint32_t byte_count)
{
    SCB_InvalidateDCache_by_Addr((uint32_t *)fatfs_sd_dma_buffer, (int32_t)byte_count);
    __DSB();
}

/**
  * @brief  清除本次 DMA 启动前遗留的传输通知。
  * @retval true 当前调用者是已登记的 Storage Task，且可安全开始一次新传输。
  * @retval false 调用者不是卷操作的唯一拥有者。
  * @note   每次只允许一个 DMA 请求在飞。清除的是通知的 pending 状态；下一次 IRQ
  *         使用 eSetValueWithOverwrite 写入完整事件值，因此旧值不会混入新传输。
  */
static bool fatfs_sd_transfer_prepare_wait(void)
{
    if ((hfatfs_sd_owner_task == NULL) ||
        (xTaskGetCurrentTaskHandle() != hfatfs_sd_owner_task))
    {
        return false;
    }

    (void)xTaskNotifyStateClearIndexed(hfatfs_sd_owner_task,
                                       FREERTOS_NOTIFY_INDEX_STORAGE_SD_TRANSFER);
    return true;
}

/**
  * @brief  阻塞等待 Storage Task 收到本次 SDMMC DMA 生命周期事件。
  * @param  event 接收 Platform 传输事件，不能为 NULL。
  * @param  timeout_ms 最大等待时间，单位为毫秒。
  * @retval true 在时限内收到了一个完整的 Platform 传输事件。
  * @retval false 调用上下文非法、参数非法或等待超时。
  * @note   索引 0 留给 SD_CD 热插拔消抖，索引 1 专属于正在飞行的 SDMMC DMA。
  *         两个索引相互独立：任务等待 DMA 时到达的插拔边沿仍会保留在索引 0，待
  *         FatFs 调用返回后由 Storage Task 主循环继续处理，不会被错误当成完成事件。
  */
static bool fatfs_sd_transfer_wait(Platform_SD_TransferEventTypeDef *event,
                                   uint32_t timeout_ms)
{
    uint32_t notification_value;

    if ((event == NULL) || (hfatfs_sd_owner_task == NULL) ||
        (xTaskGetCurrentTaskHandle() != hfatfs_sd_owner_task))
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
  * @brief  等待一个已启动传输的 IRQ 事件并推进 Platform SD 状态机。
  * @param  expected_event 本次读或写所期待的完成事件。
  * @param  timeout_ms 最大任务阻塞时间。
  * @retval true DMA 数据阶段与卡后续同步均成功。
  * @retval false 超时、错误、中止、方向不匹配或 Platform 同步失败。
  */
static bool fatfs_sd_finish_transfer(Platform_SD_TransferEventTypeDef expected_event,
                                     uint32_t timeout_ms)
{
    Platform_SD_TransferEventTypeDef event;

    if (!fatfs_sd_transfer_wait(&event, timeout_ms))
    {
        /* IRQ 未到达时仍让 Device 离开 BUSY，后续由热插拔/重初始化恢复硬件。 */
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
  * @brief  串行执行一次或多次 DMA 块读取。
  * @param  destination FatFs 提供的任意对齐接收缓冲区。
  * @param  start_block 起始逻辑块。
  * @param  block_count 总块数。
  * @param  timeout_ms 每个 DMA 子传输的最大任务阻塞时间。
  * @retval MSD_OK 所有块已经复制到 destination。
  * @retval MSD_ERROR 参数非法、DMA 启动失败、超时或完成后同步失败。
  */
static uint8_t fatfs_sd_read_blocks_dma(uint8_t *destination,
                                        uint32_t start_block,
                                        uint32_t block_count,
                                        uint32_t timeout_ms)
{
    uint32_t remaining_blocks = block_count;
    uint32_t current_block = start_block;

    if ((destination == NULL) || (block_count == 0U))
    {
        return MSD_ERROR;
    }

    while (remaining_blocks != 0U)
    {
        uint32_t chunk_blocks = remaining_blocks;
        uint32_t byte_count;

        if (chunk_blocks > FATFS_SD_DMA_BLOCK_COUNT)
        {
            chunk_blocks = FATFS_SD_DMA_BLOCK_COUNT;
        }

        byte_count = chunk_blocks * FATFS_SD_BLOCK_SIZE;
        if (!fatfs_sd_transfer_prepare_wait())
        {
            return MSD_ERROR;
        }

        fatfs_sd_dma_prepare_read(byte_count);
        if (Platform_SD_StartReadBlocks(fatfs_sd_dma_buffer,
                                        current_block,
                                        chunk_blocks) != PLATFORM_OK)
        {
            return MSD_ERROR;
        }

        if (!fatfs_sd_finish_transfer(PLATFORM_SD_TRANSFER_EVENT_READ_COMPLETE,
                                      timeout_ms))
        {
            return MSD_ERROR;
        }

        fatfs_sd_dma_finish_read(byte_count);
        (void)memcpy(destination, fatfs_sd_dma_buffer, byte_count);
        destination += byte_count;
        current_block += chunk_blocks;
        remaining_blocks -= chunk_blocks;
    }

    return MSD_OK;
}

/**
  * @brief  串行执行一次或多次 DMA 块写入。
  * @param  source FatFs 提供的任意对齐发送缓冲区。
  * @param  start_block 起始逻辑块。
  * @param  block_count 总块数。
  * @param  timeout_ms 每个 DMA 子传输的最大任务阻塞时间。
  * @retval MSD_OK 所有块已完成 DMA 数据阶段和卡内部编程确认。
  * @retval MSD_ERROR 参数非法、DMA 启动失败、超时或完成后同步失败。
  */
static uint8_t fatfs_sd_write_blocks_dma(const uint8_t *source,
                                         uint32_t start_block,
                                         uint32_t block_count,
                                         uint32_t timeout_ms)
{
    uint32_t remaining_blocks = block_count;
    uint32_t current_block = start_block;

    if ((source == NULL) || (block_count == 0U))
    {
        return MSD_ERROR;
    }

    while (remaining_blocks != 0U)
    {
        uint32_t chunk_blocks = remaining_blocks;
        uint32_t byte_count;

        if (chunk_blocks > FATFS_SD_DMA_BLOCK_COUNT)
        {
            chunk_blocks = FATFS_SD_DMA_BLOCK_COUNT;
        }

        byte_count = chunk_blocks * FATFS_SD_BLOCK_SIZE;
        (void)memcpy(fatfs_sd_dma_buffer, source, byte_count);
        if (!fatfs_sd_transfer_prepare_wait())
        {
            return MSD_ERROR;
        }

        fatfs_sd_dma_prepare_write(byte_count);
        if (Platform_SD_StartWriteBlocks(fatfs_sd_dma_buffer,
                                         current_block,
                                         chunk_blocks) != PLATFORM_OK)
        {
            return MSD_ERROR;
        }

        if (!fatfs_sd_finish_transfer(PLATFORM_SD_TRANSFER_EVENT_WRITE_COMPLETE,
                                      timeout_ms))
        {
            return MSD_ERROR;
        }

        source += byte_count;
        current_block += chunk_blocks;
        remaining_blocks -= chunk_blocks;
    }

    return MSD_OK;
}

/**
  * @brief  FatFs BSP 初始化入口。
  * @note   Platform SD 已由 Storage Task 管理卡检测和初始化；这里绝不再次直接初始化 hsd1。
  */
uint8_t BSP_SD_Init(void)
{
    Platform_SD_StateTypeDef state = Platform_SD_GetState();

    if (state == PLATFORM_SD_STATE_READY)
    {
        return MSD_OK;
    }

    return Platform_SD_IsPresent() ? MSD_ERROR : MSD_ERROR_SD_NOT_PRESENT;
}

/** @brief 卡检测 EXTI 已在 Platform SD 初始化时登记，保持 Cube BSP 契约兼容。 */
uint8_t BSP_SD_ITConfig(void)
{
    return MSD_OK;
}

/** @brief FatFs 同步读取入口，内部使用 DMA 并在 Storage Task 中阻塞等待结果。 */
uint8_t BSP_SD_ReadBlocks(uint32_t *data,
                          uint32_t start_block,
                          uint32_t block_count,
                          uint32_t timeout_ms)
{
    return fatfs_sd_read_blocks_dma((uint8_t *)data,
                                    start_block,
                                    block_count,
                                    timeout_ms);
}

/** @brief FatFs 同步写入入口，内部使用 DMA 并在 Storage Task 中阻塞等待结果。 */
uint8_t BSP_SD_WriteBlocks(uint32_t *data,
                           uint32_t start_block,
                           uint32_t block_count,
                           uint32_t timeout_ms)
{
    return fatfs_sd_write_blocks_dma((const uint8_t *)data,
                                     start_block,
                                     block_count,
                                     timeout_ms);
}

/** @brief 直接调用 BSP DMA 入口时复用同一条同步桥接，不允许裸异步返回。 */
uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *data,
                              uint32_t start_block,
                              uint32_t block_count)
{
    return fatfs_sd_read_blocks_dma((uint8_t *)data,
                                    start_block,
                                    block_count,
                                    FATFS_SD_DMA_TIMEOUT_MS);
}

/** @brief 直接调用 BSP DMA 入口时复用同一条同步桥接，不允许裸异步返回。 */
uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *data,
                               uint32_t start_block,
                               uint32_t block_count)
{
    return fatfs_sd_write_blocks_dma((const uint8_t *)data,
                                     start_block,
                                     block_count,
                                     FATFS_SD_DMA_TIMEOUT_MS);
}

/** @brief 当前未向 Platform SD 公开擦除操作。 */
uint8_t BSP_SD_Erase(uint32_t start_address, uint32_t end_address)
{
    (void)start_address;
    (void)end_address;
    return MSD_ERROR;
}

/** @brief DiskIO 在 DMA 桥返回后查询卡是否已处于可传输状态。 */
uint8_t BSP_SD_GetCardState(void)
{
    return (Platform_SD_GetState() == PLATFORM_SD_STATE_READY)
        ? SD_TRANSFER_OK
        : SD_TRANSFER_BUSY;
}

/**
  * @brief 将 Platform 介质快照转换成 Cube FatFs BSP 所需的 HAL 卡信息结构。
  */
void BSP_SD_GetCardInfo(BSP_SD_CardInfo *card_info)
{
    Platform_SD_InfoTypeDef info;

    if (card_info == NULL)
    {
        return;
    }

    (void)memset(card_info, 0, sizeof(*card_info));
    if (Platform_SD_GetInfo(&info) != PLATFORM_OK)
    {
        return;
    }

    card_info->CardType = info.CardType;
    card_info->CardVersion = info.CardVersion;
    card_info->BlockNbr = info.BlockCount;
    card_info->BlockSize = info.BlockSize;
    card_info->LogBlockNbr = info.BlockCount;
    card_info->LogBlockSize = info.BlockSize;
}

/** @brief FatFs 卡检测查询映射到 Platform 已绑定的卡检测输入。 */
uint8_t BSP_SD_IsDetected(void)
{
    return Platform_SD_IsPresent() ? SD_PRESENT : SD_NOT_PRESENT;
}

/* USER CODE END Application */
