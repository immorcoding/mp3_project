/**
  ******************************************************************************
  * @file    sd_stm32_hal_adapter.c
  * @brief   STM32H743 SDMMC1 到 SD Card Device Interface 的 Adapter。
  *
  * @details
  *          本文件只了解 STM32 HAL SD/GPIO 类型和状态，不固定使用 hsd1
  *          或某个卡检测引脚。具体 HAL Handle、检测 GPIO 和有效电平由
  *          Platform 通过 SDCard_STM32HALAdapterTypeDef 注入。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "Adapters/stm32_hal/sd/sd_stm32_hal_adapter.h"
#include "Adapters/stm32_hal/sd/sd_stm32_hal_adapter_config.h"
#include "Adapters/cortex/cache/cortex_m7_dcache_adapter.h"
#include "stm32h7xx_hal_sd.h"

#include <stddef.h>
#include <stdint.h>

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  清除当前 Adapter 保存的 DMA Cache 维护元数据。
  * @param  adapter 当前 STM32 HAL SD Adapter Context。
  * @note   该元数据不表示 SD Card Device 状态；Device 的 BUSY/READY/ERROR
  *         转换仍完全由 Components/sd 管理。
  */
static void sd_stm32_hal_clear_dma_metadata(SDCard_STM32HALAdapterTypeDef *adapter)
{
    adapter->DMABuffer = NULL;
    adapter->DMAByteCount = 0U;
    adapter->DMAReadPending = false;
}

/**
  * @brief  将连续 SD 逻辑块数转换为 DMA 传输字节数。
  * @param  block_count 连续传输的 SD 逻辑块数。
  * @param  byte_count 接收经校验的传输字节数。
  * @retval true 块数可无溢出地转换为字节数。
  * @retval false 输出参数为空、块数为零或乘法将发生 32 位无符号溢出。
  * @note   Cache line 对齐和 CMSIS API 的长度范围校验由 CortexM7DCache Module
  *         统一完成，本函数只保留 SDMMC 的逻辑块语义。
  */
static bool sd_stm32_hal_get_dma_byte_count(uint32_t block_count,
                                            uint32_t *byte_count)
{
    uint32_t transfer_size;

    if ((byte_count == NULL) ||
        (block_count == 0U) ||
        (block_count > (UINT32_MAX / SD_STM32_HAL_DMA_LOGICAL_BLOCK_SIZE)))
    {
        return false;
    }

    transfer_size = block_count * SD_STM32_HAL_DMA_LOGICAL_BLOCK_SIZE;
    *byte_count = transfer_size;
    return true;
}

/**
  * @brief  在 DMA 读取完成后使 CPU 重新从 AXI SRAM 读取新数据。
  * @param  adapter 保存本次读取 DMA 缓冲区和长度的 Adapter Context。
  * @note   只能在普通任务上下文、确认 SDMMC 数据阶段完成后调用；ISR 不得调用。
  */
static bool sd_stm32_hal_finish_dma_read(SDCard_STM32HALAdapterTypeDef *adapter)
{
    if ((!adapter->DMAReadPending) ||
        (adapter->DMABuffer == NULL) ||
        (adapter->DMAByteCount == 0U))
    {
        return true;
    }

    return CortexM7DCache_Invalidate_Aligned(adapter->DMABuffer,
                                              adapter->DMAByteCount);
}

/**
  * @brief  根据 Context 中的 GPIO 和有效电平读取卡检测状态。
  * @param  context 必须指向 SDCard_STM32HALAdapterTypeDef。
  * @retval true 检测电平与 PresentState 相同。
  * @retval false Context 无效或当前没有检测到介质。
  */
static bool sd_stm32_hal_is_present(const void *context)
{
    const SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) || (adapter->DetectPort == NULL))
    {
        return false;
    }

    return HAL_GPIO_ReadPin(adapter->DetectPort,
                            adapter->DetectPin) == adapter->PresentState;
}

/**
  * @brief  将 HAL 返回值转换为 Device 可理解的 Port 状态。
  * @param  native_status 当前 SDK 函数的立即返回状态。
  * @return 与 HAL_OK/BUSY/TIMEOUT/ERROR 对应的归一化 Port 状态。
  * @note   HAL 原始 ErrorCode 继续保留在 hsd1 中，不跨越 Port Seam。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_status(int32_t native_status)
{
    HAL_StatusTypeDef hal_status = (HAL_StatusTypeDef)native_status;

    switch (hal_status)
    {
        case HAL_OK:
            return SDCARD_PORT_OK;

        case HAL_BUSY:
            return SDCARD_PORT_BUSY;

        case HAL_TIMEOUT:
            return SDCARD_PORT_TIMEOUT;

        case HAL_ERROR:
        default:
            return SDCARD_PORT_ERROR;
    }
}

/**
  * @brief  配置 hsd1 并初始化 SDMMC1 和 SD 卡。
  * @param  context 必须指向包含 SD Handle 和卡检测 GPIO 的 Adapter Context。
  * @retval SDCARD_PORT_OK 控制器和介质初始化成功。
  * @retval SDCARD_PORT_NOT_PRESENT 初始化前或失败后检测到介质不存在。
  * @retval SDCARD_PORT_ERROR Context 无效或 HAL 初始化失败。
  * @note   参数与当前 CubeMX SDMMC1 配置保持一致。以后在 CubeMX 修改总线
  *         宽度、时钟沿或分频时，必须同步检查本函数。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_init(void *context)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) || (adapter->Handle == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    sd_stm32_hal_clear_dma_metadata(adapter);

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    /*
     * 不调用 CubeMX 的 void MX_SDMMC1_SD_Init()：该函数在失败时直接进入
     * Error_Handler()，不适合可移除介质。这里保留同一份硬件参数，但让
     * HAL_StatusTypeDef 能沿 Port -> Device -> Platform 正常返回。
     */
    hal_sd->Instance = SDMMC1;
    hal_sd->Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hal_sd->Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hal_sd->Init.BusWide = SDMMC_BUS_WIDE_4B;
    hal_sd->Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    hal_sd->Init.ClockDiv = 0u;

    HAL_StatusTypeDef hal_status = HAL_SD_Init(hal_sd);
    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

    /* 初始化期间被拔卡时，优先报告介质不存在而不是泛化 HAL_ERROR。 */
    if ((hal_status != HAL_OK) && (!sd_stm32_hal_is_present(context)))
    {
        status = SDCARD_PORT_NOT_PRESENT;
    }

    if (hal_status != HAL_OK)
    {
        /* 尽力清理 MSP 时钟和 GPIO；保留清理前捕获的原始失败信息。 */
        (void)HAL_SD_DeInit(hal_sd);
    }

    return status;
}

/**
  * @brief  反初始化当前 Context 指向的 SD Handle 和对应 MSP 资源。
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @return HAL_SD_DeInit() 转换后的归一化 Port 状态。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_deinit(void *context)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) || (adapter->Handle == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    sd_stm32_hal_clear_dma_metadata(adapter);
    return sd_stm32_hal_status((int32_t)HAL_SD_DeInit(adapter->Handle));
}

/**
  * @brief  将 HAL 卡信息转换为 Device 的逻辑块信息。
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @param  info 接收归一化容量、块大小、卡类型和版本信息。
  * @retval SDCARD_PORT_OK 信息读取并转换成功。
  * @retval SDCARD_PORT_NOT_PRESENT 当前没有检测到介质。
  * @retval SDCARD_PORT_ERROR 参数或 HAL 查询失败。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_get_info(
    void *context,
    SDCard_InfoTypeDef *info)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;
    HAL_SD_CardInfoTypeDef hal_info;

    if ((adapter == NULL) || (adapter->Handle == NULL) || (info == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    HAL_StatusTypeDef hal_status = HAL_SD_GetCardInfo(hal_sd, &hal_info);
    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

    if (status != SDCARD_PORT_OK)
    {
        return status;
    }

    info->BlockCount = hal_info.LogBlockNbr;
    info->BlockSize = hal_info.LogBlockSize;
    info->CapacityBytes = (uint64_t)hal_info.LogBlockNbr * (uint64_t)hal_info.LogBlockSize;
    info->CardType = hal_info.CardType;
    info->CardVersion = hal_info.CardVersion;

    return status;
}

/**
  * @brief  使用 HAL 轮询接口读取一个或多个逻辑块。
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @param  data 接收数据的缓冲区。
  * @param  start_block 起始逻辑块编号。
  * @param  block_count 连续读取的逻辑块数量。
  * @param  timeout_ms HAL 数据阶段超时时间。
  * @return HAL 结果转换后的 Port 状态；传输中拔卡优先返回 NOT_PRESENT。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_read_blocks(
    void *context,
    uint8_t *data,
    uint32_t start_block,
    uint32_t block_count,
    uint32_t timeout_ms)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (data == NULL) ||
        (block_count == 0u))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    HAL_StatusTypeDef hal_status = HAL_SD_ReadBlocks(hal_sd,
                                                     data,
                                                     start_block,
                                                     block_count,
                                                     timeout_ms);
    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

    if ((status != SDCARD_PORT_OK) &&
        (!sd_stm32_hal_is_present(context)))
    {
        status = SDCARD_PORT_NOT_PRESENT;
    }

    return status;
}

/**
  * @brief  使用 HAL 轮询接口写入一个或多个逻辑块。
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @param  data 提供数据的只读缓冲区。
  * @param  start_block 起始逻辑块编号。
  * @param  block_count 连续写入的逻辑块数量。
  * @param  timeout_ms HAL 数据阶段超时时间。
  * @return HAL 结果转换后的 Port 状态；传输中拔卡优先返回 NOT_PRESENT。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_write_blocks(
    void *context,
    const uint8_t *data,
    uint32_t start_block,
    uint32_t block_count,
    uint32_t timeout_ms)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (data == NULL) ||
        (block_count == 0u))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    HAL_StatusTypeDef hal_status = HAL_SD_WriteBlocks(hal_sd,
                                                      data,
                                                      start_block,
                                                      block_count,
                                                      timeout_ms);
    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

    if ((status != SDCARD_PORT_OK) &&
        (!sd_stm32_hal_is_present(context)))
    {
        status = SDCARD_PORT_NOT_PRESENT;
    }

    return status;
}

/**
  * @brief  通过 STM32H7 SDMMC 内部 DMA 启动非阻塞块读取。
  * @param  context 指向已装配的 STM32 HAL SD Adapter Context。
  * @param  data SDMMC IDMA 将写入的缓冲区。
  * @param  start_block 起始逻辑块编号。
  * @param  block_count 连续逻辑块数量。
  * @retval SDCARD_PORT_OK SDMMC 已接受 DMA 请求。
  * @retval 其他值 参数无效、无卡或 HAL 拒绝本次启动。
  * @note   此处不能等待 HAL_SD_CARD_TRANSFER，也不能修改 SD Card Device 状态。
  *         HAL 完成或错误回调由 IRQ Adapter 分发给 Platform，再由调用者的普通
  *         任务上下文完成同步和状态推进。Cache 一致性由本 Adapter 维护；缓冲区
  *         必须满足 32 字节对齐和整 Cache line 长度的约束。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_read_blocks_dma(
    void *context,
    uint8_t *data,
    uint32_t start_block,
    uint32_t block_count)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;
    uint32_t byte_count;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (!sd_stm32_hal_get_dma_byte_count(block_count, &byte_count)))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    if (!CortexM7DCache_CleanInvalidate_Aligned(data, byte_count))
    {
        return SDCARD_PORT_ERROR;
    }

    adapter->DMABuffer = data;
    adapter->DMAByteCount = byte_count;
    adapter->DMAReadPending = true;

    HAL_StatusTypeDef hal_status = HAL_SD_ReadBlocks_DMA(hal_sd,
                                                         data,
                                                         start_block,
                                                         block_count);

    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

    if ((status != SDCARD_PORT_OK) &&
        (!sd_stm32_hal_is_present(context)))
    {
        status = SDCARD_PORT_NOT_PRESENT;
    }

    if (status != SDCARD_PORT_OK)
    {
        sd_stm32_hal_clear_dma_metadata(adapter);
    }

    return status;
}

/**
  * @brief  通过 STM32H7 SDMMC 内部 DMA 启动非阻塞块写入。
  * @param  context 指向已装配的 STM32 HAL SD Adapter Context。
  * @param  data SDMMC IDMA 将读取的缓冲区；完成前内容必须保持稳定。
  * @param  start_block 起始逻辑块编号。
  * @param  block_count 连续逻辑块数量。
  * @retval SDCARD_PORT_OK SDMMC 已接受 DMA 请求。
  * @retval 其他值 参数无效、无卡或 HAL 拒绝本次启动。
  * @note   本 Adapter 在启动前执行 D-Cache Clean。为了避免扩大 Cache 操作范围
  *         影响相邻变量，缓冲区必须满足 32 字节对齐和整 Cache line 长度的约束。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_write_blocks_dma(
    void *context,
    const uint8_t *data,
    uint32_t start_block,
    uint32_t block_count)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;
    uint32_t byte_count;

    if ((adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (!sd_stm32_hal_get_dma_byte_count(block_count, &byte_count)))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;

    if (!sd_stm32_hal_is_present(context))
    {
        return SDCARD_PORT_NOT_PRESENT;
    }

    if (!CortexM7DCache_Clean_Aligned(data, byte_count))
    {
        return SDCARD_PORT_ERROR;
    }

    adapter->DMABuffer = (uint8_t *)(uintptr_t)data;
    adapter->DMAByteCount = byte_count;
    adapter->DMAReadPending = false;

    HAL_StatusTypeDef hal_status = HAL_SD_WriteBlocks_DMA(hal_sd,
                                                          data,
                                                          start_block,
                                                          block_count);

    SDCard_PortStatusTypeDef status = sd_stm32_hal_status((int32_t)hal_status);

    if ((status != SDCARD_PORT_OK) &&
        (!sd_stm32_hal_is_present(context)))
    {
        status = SDCARD_PORT_NOT_PRESENT;
    }

    if (status != SDCARD_PORT_OK)
    {
        sd_stm32_hal_clear_dma_metadata(adapter);
    }

    return status;
}

/**
  * @brief  轮询卡状态，直到回到 HAL_SD_CARD_TRANSFER。
  * @details
  *         HAL 的阻塞读写完成数据搬运后，卡内部仍可能处于 PROGRAMMING。
  *         本函数让 Device 成功返回前确认介质真正可以接受下一条命令。
  * @param  context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @param  timeout_ms 等待介质回到 TRANSFER 状态的最长时间。
  * @retval SDCARD_PORT_OK 介质已经回到可传输状态。
  * @retval SDCARD_PORT_NOT_PRESENT 等待期间介质被拔出。
  * @retval SDCARD_PORT_ERROR HAL 报告错误或断开状态。
  * @retval SDCARD_PORT_TIMEOUT 等待时间达到上限。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_sync(
    void *context,
    uint32_t timeout_ms)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;
    uint32_t start_tick;

    if ((adapter == NULL) || (adapter->Handle == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    SD_HandleTypeDef *hal_sd = adapter->Handle;
    start_tick = HAL_GetTick();

    while (true)
    {
        if (!sd_stm32_hal_is_present(context))
        {
            sd_stm32_hal_clear_dma_metadata(adapter);
            return SDCARD_PORT_NOT_PRESENT;
        }

        HAL_SD_CardStateTypeDef card_state = HAL_SD_GetCardState(hal_sd);

        if (card_state == HAL_SD_CARD_TRANSFER)
        {
            if (!sd_stm32_hal_finish_dma_read(adapter))
            {
                sd_stm32_hal_clear_dma_metadata(adapter);
                return SDCARD_PORT_ERROR;
            }

            sd_stm32_hal_clear_dma_metadata(adapter);
            return SDCARD_PORT_OK;
        }

        if ((card_state == HAL_SD_CARD_ERROR) ||
            (card_state == HAL_SD_CARD_DISCONNECTED))
        {
            sd_stm32_hal_clear_dma_metadata(adapter);
            return SDCARD_PORT_ERROR;
        }

        /* 无符号时间差可安全跨越 HAL_GetTick() 的 32 位回绕点。 */
        if ((uint32_t)(HAL_GetTick() - start_tick) >= timeout_ms)
        {
            sd_stm32_hal_clear_dma_metadata(adapter);
            return SDCARD_PORT_TIMEOUT;
        }
    }
}

/**
  * @brief  判断自 start_tick 起经过的毫秒数是否仍在预算之内。
  * @param[in] start_tick 起点，来自 HAL_GetTick()。
  * @param[in] timeout_ms 预算；0 表示一点时间都没有，恒为 false。
  * @retval true 还没到点。
  * @note   无符号差可安全跨越 HAL_GetTick() 的 32 位回绕点；精度为 1 ms，所以实际上界在
  *         timeout_ms-1 到 timeout_ms 之间。
  */
static bool sd_stm32_hal_in_budget(uint32_t start_tick, uint32_t timeout_ms)
{
    return (uint32_t)(HAL_GetTick() - start_tick) < timeout_ms;
}

/**
  * @brief  由 HAL Handle 的外设实例得到对应的 NVIC 中断号。
  * @param[in] hal_sd 当前 SD Handle。
  * @return SDMMC2 返回 SDMMC2_IRQn，其余按 SDMMC1 处理（与本 Adapter 固定使用 SDMMC1 一致）。
  */
static IRQn_Type sd_stm32_hal_irqn(const SD_HandleTypeDef *hal_sd)
{
    return (hal_sd->Instance == SDMMC2) ? SDMMC2_IRQn : SDMMC1_IRQn;
}

/**
  * @brief  对 HAL Handle 的 SDMMC 外设做一次 RCC 复位脉冲，清掉 HAL_SD_DeInit() 留下的寄存器状态。
  * @param[in] hal_sd 当前 SD Handle。
  * @note   SDMMC1 在 AHB3RSTR 的 SDMMC1RST，SDMMC2 在 AHB2RSTR 的 SDMMC2RST（宏名和实例对应读自
  *         stm32h7xx_hal_rcc.h）。在 HAL_SD_DeInit() 之后调用，此时外设时钟已被 MspDeInit 关掉；复位不依赖外设时钟，
  *         依据是 HAL_DeInit() 也在不先使能时钟的情况下复位整条总线上的外设——这一点没有实测。
  *         复位之后 MASK、IDMACTRL、DCTRL、POWER 等回到复位值，所以重新插卡走完整的初始化。
  */
static void sd_stm32_hal_reset_peripheral(const SD_HandleTypeDef *hal_sd)
{
    if (hal_sd->Instance == SDMMC2)
    {
        __HAL_RCC_SDMMC2_FORCE_RESET();
        __HAL_RCC_SDMMC2_RELEASE_RESET();
    }
    else
    {
        __HAL_RCC_SDMMC1_FORCE_RESET();
        __HAL_RCC_SDMMC1_RELEASE_RESET();
    }
}

/**
  * @brief  发送一条短响应命令，并在预算内等它结束（命令通路不再活动）。
  * @param[in,out] hal_sd 当前 SD Handle。
  * @param[in] command_index 命令序号（本函数只用于 CMD12 与 CMD13）。
  * @param[in] argument 命令参数。
  * @param[in] start_tick 本次中止的起点，来自 HAL_GetTick()。
  * @param[in] timeout_ms 本次中止的总预算。
  * @param[out] status_flags 接收命令结束时的 STA 快照，可为空。
  * @retval true 命令通路在预算内结束（含响应超时与 CRC 错误，由调用者按 status_flags 判断）。
  * @retval false 到预算还没结束（命令通路卡住）。
  * @note   这是 SDMMC_GetCmdResp1() 的有上界版本：HAL 的循环次数上界按 SystemCoreClock 折算，
  *         STOP_TRANSMISSION 用的 SDMMC_STOPTRANSFERTIMEOUT 乘法在 32 位里会回绕，不能当作可预期的上界。
  */
static bool sd_stm32_hal_send_command_bounded(SD_HandleTypeDef *hal_sd,
                                              uint32_t command_index,
                                              uint32_t argument,
                                              uint32_t start_tick,
                                              uint32_t timeout_ms,
                                              uint32_t *status_flags)
{
    SDMMC_CmdInitTypeDef command;
    uint32_t sta;

    command.Argument = argument;
    command.CmdIndex = command_index;
    command.Response = SDMMC_RESPONSE_SHORT;
    command.WaitForInterrupt = SDMMC_WAIT_NO;
    command.CPSM = SDMMC_CPSM_ENABLE;
    (void)SDMMC_SendCommand(hal_sd->Instance, &command);

    for (;;)
    {
        sta = hal_sd->Instance->STA;
        if (((sta & (SDMMC_FLAG_CCRCFAIL | SDMMC_FLAG_CMDREND | SDMMC_FLAG_CTIMEOUT | SDMMC_FLAG_BUSYD0END)) != 0U) &&
            ((sta & SDMMC_FLAG_CMDACT) == 0U))
        {
            break;
        }

        if (!sd_stm32_hal_in_budget(start_tick, timeout_ms))
        {
            return false;
        }
    }

    if (status_flags != NULL)
    {
        *status_flags = sta;
    }

    __SDMMC_CLEAR_FLAG(hal_sd->Instance, SDMMC_STATIC_CMD_FLAGS);
    return true;
}

/**
  * @brief  轮询卡状态（CMD13），直到回到 TRANSFER；每一步都受同一个预算约束。
  * @param[in] context 指向 Adapter Context，用于读卡检测。
  * @param[in,out] hal_sd 当前 SD Handle，使用其中的相对卡地址。
  * @param[in] start_tick 本次中止的起点。
  * @param[in] timeout_ms 本次中止的总预算。
  * @retval true 卡在预算内回到 TRANSFER。
  * @retval false 到预算还没回到 TRANSFER、命令通路卡住，或卡已被拔出。
  */
static bool sd_stm32_hal_wait_card_transfer(const void *context,
                                            SD_HandleTypeDef *hal_sd,
                                            uint32_t start_tick,
                                            uint32_t timeout_ms)
{
    uint32_t sta;

    for (;;)
    {
        if (!sd_stm32_hal_send_command_bounded(hal_sd,
                                               SDMMC_CMD_SEND_STATUS,
                                               (uint32_t)(hal_sd->SdCard.RelCardAdd << 16U),
                                               start_tick,
                                               timeout_ms,
                                               &sta))
        {
            return false;
        }

        if (((sta & (SDMMC_FLAG_CTIMEOUT | SDMMC_FLAG_CCRCFAIL)) == 0U) &&
            (SDMMC_GetCommandResponse(hal_sd->Instance) == SDMMC_CMD_SEND_STATUS))
        {
            const uint32_t card_state =
                (SDMMC_GetResponse(hal_sd->Instance, SDMMC_RESP1) >> 9U) & 0x0FU;

            if (card_state == HAL_SD_CARD_TRANSFER)
            {
                return true;
            }
        }

        if ((!sd_stm32_hal_is_present(context)) ||
            (!sd_stm32_hal_in_budget(start_tick, timeout_ms)))
        {
            return false;
        }
    }
}

/**
  * @brief  让数据通路静止：发 CMD12 终止传输、等 DPSM 回到空闲、关 IDMA、复位 FIFO、清标志，再确认卡回到 TRANSFER。
  * @param[in] context 指向 Adapter Context。
  * @param[in,out] hal_sd 当前 SD Handle。
  * @param[in] start_tick 本次中止的起点。
  * @param[in] timeout_ms 本次中止的总预算。
  * @retval true 全部步骤在预算内完成。
  * @retval false 任一步到预算还没完成；硬件状态未定，调用者必须反初始化 SDMMC。
  * @note   顺序依据 HAL_SD_Abort() 与 HAL_SD_IRQHandler() 的错误分支，等待全部改成有上界：
  *         1. CMD12（CMDSTOP 置位、CMDTRANS 清零）向 DPSM 发中止信号；响应超时与 CRC 错误不算失败——
  *            传输可能刚好自己结束，卡不再响应 CMD12（HAL 同样容忍响应超时）；
  *         2. 等 STA.DPSMACT 回到 0。IDMA 使能位只能在 DPSM 静止之后写（HAL 注释：不等标志，
  *            后面的 IDMACTRL 清零不会生效，下一笔读写会失败），所以这一步必须先于第 3 步；
  *         3. IDMACTRL 清零并回读确认；DCTRL.FIFORST 复位 FIFO（尽力而为，HAL 错误分支同样使用）；
  *         4. 清全部静态标志；
  *         5. CMD13 轮询直到卡回到 TRANSFER（写入方向的 R1b 忙由此等掉）。
  *         已经自己结束的传输（DPSM 静止且 IDMA 已关）跳过第 1、2 步。
  */
static bool sd_stm32_hal_quiesce_data_path(const void *context,
                                           SD_HandleTypeDef *hal_sd,
                                           uint32_t start_tick,
                                           uint32_t timeout_ms)
{
    SDMMC_TypeDef *sdmmc = hal_sd->Instance;
    const bool needs_stop = ((sdmmc->STA & SDMMC_FLAG_DPSMACT) != 0U) ||
                            ((sdmmc->IDMACTRL & SDMMC_IDMA_IDMAEN) != 0U);

    __SDMMC_CMDTRANS_DISABLE(sdmmc);

    if (needs_stop)
    {
        bool command_finished;

        __SDMMC_CMDSTOP_ENABLE(sdmmc);
        command_finished = sd_stm32_hal_send_command_bounded(hal_sd,
                                                             SDMMC_CMD_STOP_TRANSMISSION,
                                                             0U,
                                                             start_tick,
                                                             timeout_ms,
                                                             NULL);
        __SDMMC_CMDSTOP_DISABLE(sdmmc);
        if (!command_finished)
        {
            return false;
        }

        while ((sdmmc->STA & SDMMC_FLAG_DPSMACT) != 0U)
        {
            if (!sd_stm32_hal_in_budget(start_tick, timeout_ms))
            {
                return false;
            }
        }
    }

    sdmmc->IDMACTRL = SDMMC_DISABLE_IDMA;
    if ((sdmmc->IDMACTRL & SDMMC_IDMA_IDMAEN) != 0U)
    {
        return false;
    }

    sdmmc->DCTRL |= SDMMC_DCTRL_FIFORST;
    __HAL_SD_CLEAR_FLAG(hal_sd, SDMMC_STATIC_FLAGS);

    return sd_stm32_hal_wait_card_transfer(context, hal_sd, start_tick, timeout_ms);
}

/**
  * @brief  在上界内中止在飞的 DMA 传输；停不住就反初始化 SDMMC。不调用 HAL_SD_Abort()。
  * @param[in,out] context 必须指向有效 SDCard_STM32HALAdapterTypeDef。
  * @param[in] timeout_ms 从调用起算的总预算，单位为毫秒，覆盖 CMD12、DPSM 静止与卡回到 TRANSFER 全部等待。
  * @retval SDCARD_PORT_OK 传输已停住；HAL Handle 的 State 回到 READY、Context 回到 NONE、ErrorCode 清零，
  *         读方向的 DMA 缓冲区已作废 D-Cache；Port 仍可继续使用。
  * @retval SDCARD_PORT_NOT_PRESENT 开始时或结束时卡已不在；已 HAL_SD_DeInit。
  * @retval SDCARD_PORT_TIMEOUT 到预算还没停住；已 HAL_SD_DeInit。
  * @retval SDCARD_PORT_ERROR Context 无效，或停住之后作废 D-Cache 失败（后者已 HAL_SD_DeInit）。
  * @note   HAL_SD_Abort() 的数据超时常量 SDMMC_SWDATATIMEOUT 是 0xFFFFFFFF ms，等于没有上界，所以这里自己写序列，
  *         并自己恢复 HAL Handle 的 State、Context（HAL 升级时要复核，见 README）。
  *
  *         与 SDMMC 中断的竞争：第一步先把 SDMMC 的中断屏蔽寄存器清零，之后外设不再为被中止的传输请求中断；
  *         此前已经发生的中断在任务被打断之前就整个跑完了（单核，任务不会与 ISR 同时运行），其效果是 State 已经
  *         回到 READY 和完成回调已经给出信号，本函数照常收尾，Service 在中止返回之后再清一次完成信号量。
  *         收尾时清 NVIC 挂起位，避免重新使能后冒出一次过期的中断。
  *
  *         D-Cache：必须先确认 IDMA 已关（quiesce 的第 3 步回读）再作废读缓冲区的缓存行，否则迟到的 DMA 写入会落在
  *         刚作废过的行之后。反初始化路径不作废缓存：硬件状态未定，缓冲区此后由下一笔传输开始前的
  *         Clean+Invalidate 重新建立一致性。
  */
static SDCard_PortStatusTypeDef sd_stm32_hal_abort_transfer(void *context, uint32_t timeout_ms)
{
    SDCard_STM32HALAdapterTypeDef *adapter = context;
    SD_HandleTypeDef *hal_sd;
    uint32_t start_tick;
    SDCard_PortStatusTypeDef result = SDCARD_PORT_OK;

    if ((adapter == NULL) || (adapter->Handle == NULL))
    {
        return SDCARD_PORT_ERROR;
    }

    hal_sd = adapter->Handle;
    start_tick = HAL_GetTick();

    /* 第一步：外设不再为这笔传输产生中断。HAL_SD_IRQHandler() 读的是原始 STA 而不是被屏蔽后的状态，
     * 所以 NVIC 里残留的挂起位在最后统一清掉。 */
    hal_sd->Instance->MASK = 0U;

    if (!sd_stm32_hal_is_present(context))
    {
        result = SDCARD_PORT_NOT_PRESENT;
    }
    else if (!sd_stm32_hal_quiesce_data_path(context, hal_sd, start_tick, timeout_ms))
    {
        result = sd_stm32_hal_is_present(context) ? SDCARD_PORT_TIMEOUT : SDCARD_PORT_NOT_PRESENT;
    }
    else if ((adapter->DMAReadPending) &&
             (adapter->DMABuffer != NULL) &&
             (adapter->DMAByteCount != 0U) &&
             (!CortexM7DCache_Invalidate_Aligned(adapter->DMABuffer, adapter->DMAByteCount)))
    {
        result = SDCARD_PORT_ERROR;
    }
    else
    {
        /* 与 HAL_SD_Abort() 成功路径一致：State 回 READY、Context 回 NONE；ErrorCode 清零。 */
        hal_sd->ErrorCode = HAL_SD_ERROR_NONE;
        hal_sd->State = HAL_SD_STATE_READY;
        hal_sd->Context = SD_CONTEXT_NONE;
    }

    sd_stm32_hal_clear_dma_metadata(adapter);

    if (result != SDCARD_PORT_OK)
    {
        /* 没有干净地停住：断电并关时钟，再对外设做一次 RCC 复位，卷由 Device 保持 ERROR（或 NOT_PRESENT），
         * 重新初始化才恢复。HAL_SD_DeInit() 不复位 SDMMC 寄存器，卡死的 IDMA/DPSM 会留着，重插后 HAL_SD_Init()
         * 的 SCR 轮询读可能被残留的 IDMA 写进 bounce buffer。 */
        (void)HAL_SD_DeInit(hal_sd);
        sd_stm32_hal_reset_peripheral(hal_sd);
    }

    NVIC_ClearPendingIRQ(sd_stm32_hal_irqn(hal_sd));
    return result;
}

/* Private variables ---------------------------------------------------------*/
/**
  * @brief SD Card Device 使用的 STM32 HAL SDMMC Port 操作表。
  * @note  具体 SD Handle、卡检测 GPIO 和极性通过 PortContext 注入。
  */
static const SDCard_PortOpsTypeDef sd_stm32_hal_ops = {
    .IsPresent = sd_stm32_hal_is_present,
    .Init = sd_stm32_hal_init,
    .DeInit = sd_stm32_hal_deinit,
    .GetInfo = sd_stm32_hal_get_info,
    .ReadBlocks = sd_stm32_hal_read_blocks,
    .WriteBlocks = sd_stm32_hal_write_blocks,
    .StartReadBlocks = sd_stm32_hal_read_blocks_dma,
    .StartWriteBlocks = sd_stm32_hal_write_blocks_dma,
    .Sync = sd_stm32_hal_sync,
    .AbortTransfer = sd_stm32_hal_abort_transfer
};

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  将 STM32 HAL Adapter 的 Ops 和具体实例安装到 Device Handle。
  * @param  hsdcard 待绑定的 SD Card Device Handle。
  * @param  adapter Platform 层装配的 HAL Handle 和卡检测配置。
  * @retval SDCARD_OK 绑定成功。
  * @retval SDCARD_ERROR Device Handle、Adapter Context 或必需资源无效。
  * @note   本函数只完成依赖装配，不初始化 SDMMC1，也不访问 SD 卡。
  */
SDCard_StatusTypeDef SDCard_STM32HALAdapter_Bind(
    SDCard_HandleTypeDef *hsdcard,
    SDCard_STM32HALAdapterTypeDef *adapter)
{
    if ((hsdcard == NULL) ||
        (adapter == NULL) ||
        (adapter->Handle == NULL) ||
        (adapter->DetectPort == NULL) ||
        (adapter->DetectPin == 0u))
    {
        return SDCARD_ERROR;
    }

    hsdcard->PortOps = &sd_stm32_hal_ops;
    hsdcard->PortContext = adapter;
    return SDCARD_OK;
}
