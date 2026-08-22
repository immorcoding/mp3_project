/**
  ******************************************************************************
  * @file    st7789_spi_stm32_hal_adapter.c
  * @brief   STM32 HAL SPI/GPIO 到 ST7789 PortOps 的 Adapter 实现。
  *
  * @details
 *          本 Adapter 负责 HAL 状态归一化、SPI 阻塞传输、DMA 分块续传、
 *          D-Cache Clean 以及 CS/D-C/RESET 电平映射。SPI 实例、GPIO 和
 *          有效电平均由 Platform 注入，ST7789 Device 不需要认识 STM32 HAL。
  ******************************************************************************
  */

#include "Adapters/stm32_hal/st7789_spi/st7789_spi_stm32_hal_adapter.h"
#include "Adapters/stm32_hal/st7789_spi/st7789_spi_stm32_hal_adapter_config.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "Adapters/cortex/cache/cortex_m7_dcache_adapter.h"

/** @brief 当前唯一 ST7789 SPI Adapter 的异步传输私有状态。 */
typedef struct
{
    const uint16_t *Pixels; /**< 下一 DMA 块的 RGB565 像素首地址。 */
    uint32_t RemainingPixels; /**< 尚未启动的 RGB565 像素数。 */
    ST7789_PortTransferCallback_t Callback; /**< 最终结果回调。 */
    void *CallbackContext; /**< 最终结果回调的不透明上下文。 */
    volatile bool Active; /**< true 表示当前存在尚未完成的 DMA 事务。 */
} st7789_spi_stm32_hal_transfer_state_type;

/**
 * @brief 由当前工程唯一 LCD SPI1 Adapter 借用的 HAL 回调归属。
 * @note  HAL SPI 的 Register Callback Interface 只保存每个 Handle 的一个函数指针。
 *        现阶段仅有一个 ST7789 Adapter 使用 SPI1，故把该归属收敛在本 Module；
 *        将来出现第二个实际异步 SPI 使用者时，再按 Handle 提取专用 IRQ 分发 Module。
 */
static ST7789_SPI_STM32HALAdapterTypeDef *volatile hst7789_spi_stm32_hal_adapter;

/** @brief 当前 ST7789 SPI DMA 事务的私有续传状态。 */
static st7789_spi_stm32_hal_transfer_state_type hst7789_spi_stm32_hal_transfer;

static void st7789_spi_stm32_hal_tx_complete(SPI_HandleTypeDef *hspi);
static void st7789_spi_stm32_hal_error(SPI_HandleTypeDef *hspi);

/**
 * @brief  在 SPI 空闲时切换当前串行帧宽并重新应用 HAL 配置。
 * @param  adapter 已由 Platform 长期持有的 LCD SPI Adapter。
 * @param  data_size 目标 SPI 数据帧宽，仅允许 8-bit 或 16-bit。
 * @retval ST7789_PORT_OK 已处于或已成功切换到目标帧宽。
 * @retval ST7789_PORT_BUSY SPI 仍在传输，不能修改 DataSize。
 * @retval ST7789_PORT_ERROR 参数或 HAL 重新初始化失败。
 * @note   ST7789 命令、命令参数和读 ID 均使用 8-bit；仅 RGB565 DMA 使用
 *         16-bit。HAL_SPI_Init() 在 Handle 已处于 READY 而非 RESET 时不会
 *         重置已注册的 Tx/Error 完成回调，因此 DMA 回调归属可跨帧宽切换保留。
 */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_select_data_size(
    ST7789_SPI_STM32HALAdapterTypeDef *adapter,
    uint32_t data_size)
{
    SPI_HandleTypeDef *hspi;
    uint32_t previous_data_size;

    if ((adapter == NULL) || (adapter->SPIHandle == NULL) ||
        ((data_size != SPI_DATASIZE_8BIT) && (data_size != SPI_DATASIZE_16BIT)))
    {
        return ST7789_PORT_ERROR;
    }

    hspi = adapter->SPIHandle;

    if (hspi->Init.DataSize == data_size)
    {
        return ST7789_PORT_OK;
    }

    if (HAL_SPI_GetState(hspi) != HAL_SPI_STATE_READY)
    {
        return ST7789_PORT_BUSY;
    }

    previous_data_size = hspi->Init.DataSize;
    hspi->Init.DataSize = data_size;

    if (HAL_SPI_Init(hspi) != HAL_OK)
    {
        hspi->Init.DataSize = previous_data_size;
        return ST7789_PORT_ERROR;
    }

    return ST7789_PORT_OK;
}

/**
  * @brief  将 STM32 HAL 状态转换为 ST7789 Device 可理解的端口状态。
  * @param  native_status HAL_StatusTypeDef 的整数表示。
  * @retval ST7789_PortStatusTypeDef 归一化后的端口结果。
  */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_status(int32_t native_status)
{
    switch ((HAL_StatusTypeDef)native_status)
    {
        case HAL_OK:
            return ST7789_PORT_OK;

        case HAL_BUSY:
            return ST7789_PORT_BUSY;

        case HAL_TIMEOUT:
            return ST7789_PORT_TIMEOUT;

        case HAL_ERROR:
        default:
            return ST7789_PORT_ERROR;
    }
}

/**
  * @brief  返回给定有效电平对应的非有效电平。
  * @param  active_state 信号有效时的平台电平。
  * @retval GPIO_PIN_RESET 或 GPIO_PIN_SET 的相反电平。
  */
static GPIO_PinState st7789_spi_stm32_hal_inactive_state(GPIO_PinState active_state)
{
    return (active_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

/**
 * @brief  清空当前 DMA 事务并把最终结果发布给已绑定的 ST7789 Device。
 * @param  status 当前事务的归一化完成结果。
 * @note   本函数由 SPI HAL 回调在 ISR 上下文调用。先清除内部状态，再调用
 *         Device 回调，避免上层任务被唤醒后观察到过期的在飞事务。
 */
static void st7789_spi_stm32_hal_finish_transfer(ST7789_PortStatusTypeDef status)
{
    ST7789_PortTransferCallback_t callback = hst7789_spi_stm32_hal_transfer.Callback;
    void *callback_context = hst7789_spi_stm32_hal_transfer.CallbackContext;

    hst7789_spi_stm32_hal_transfer.Active = false;
    hst7789_spi_stm32_hal_transfer.Pixels = NULL;
    hst7789_spi_stm32_hal_transfer.RemainingPixels = 0u;
    /*
     * 回调是绑定期安装、跨多帧复用的 Adapter 归属；不能随单笔事务清除。
     * 否则下一帧会因未注册回调而无法启动 DMA。
     */
    __DMB();

    if (callback != NULL)
    {
        callback(status, callback_context);
    }
}

/**
 * @brief  启动当前异步事务的下一块 16-bit HAL SPI DMA 像素发送。
 * @retval ST7789_PORT_OK 下一块已提交给 HAL SPI DMA。
 * @retval 其他值 当前状态非法或 HAL 无法接受下一块。
 * @note   调用前先推进私有指针和剩余计数，避免 DMA 完成 IRQ 在
 *         HAL_SPI_Transmit_DMA() 返回前到达时重复发送同一块。若 HAL 启动失败，
 *         再恢复这两个字段。
 */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_start_next_chunk(void)
{
    const uint16_t *pixels;
    uint32_t remaining_pixels;
    uint32_t chunk_pixels;
    HAL_StatusTypeDef hal_status;

    if ((hst7789_spi_stm32_hal_adapter == NULL) ||
        (hst7789_spi_stm32_hal_adapter->SPIHandle == NULL) ||
        (!hst7789_spi_stm32_hal_transfer.Active) ||
        (hst7789_spi_stm32_hal_transfer.Pixels == NULL) ||
        (hst7789_spi_stm32_hal_transfer.RemainingPixels == 0u))
    {
        return ST7789_PORT_ERROR;
    }

    pixels = hst7789_spi_stm32_hal_transfer.Pixels;
    remaining_pixels = hst7789_spi_stm32_hal_transfer.RemainingPixels;
    chunk_pixels = (remaining_pixels > ST7789_SPI_STM32_HAL_DMA_CHUNK_PIXELS)
                       ? ST7789_SPI_STM32_HAL_DMA_CHUNK_PIXELS
                       : remaining_pixels;

    if ((chunk_pixels == 0u) || (chunk_pixels > UINT16_MAX))
    {
        return ST7789_PORT_ERROR;
    }

    hst7789_spi_stm32_hal_transfer.Pixels = pixels + chunk_pixels;
    hst7789_spi_stm32_hal_transfer.RemainingPixels =
        remaining_pixels - chunk_pixels;
    __DMB();

    hal_status = HAL_SPI_Transmit_DMA(
        hst7789_spi_stm32_hal_adapter->SPIHandle,
        (const uint8_t *)pixels,
        (uint16_t)chunk_pixels);
    if (hal_status != HAL_OK)
    {
        hst7789_spi_stm32_hal_transfer.Pixels = pixels;
        hst7789_spi_stm32_hal_transfer.RemainingPixels = remaining_pixels;
        __DMB();
    }

    return st7789_spi_stm32_hal_status((int32_t)hal_status);
}

/**
 * @brief  响应 HAL SPI 的最终 EOT 回调，续发下一块或结束整个 RAMWR 事务。
 * @param  hspi HAL 传入的 SPI Handle。
 * @note   DMA Stream 完成仅表示数据已写入 SPI FIFO；H7 HAL 会在 SPI EOT
 *         中断中调用本回调，此时最后一位也已经移出 MOSI，才能安全续传或释放 CS。
 */
static void st7789_spi_stm32_hal_tx_complete(SPI_HandleTypeDef *hspi)
{
    ST7789_PortStatusTypeDef status;

    if ((hst7789_spi_stm32_hal_adapter == NULL) ||
        (hspi != hst7789_spi_stm32_hal_adapter->SPIHandle) ||
        (!hst7789_spi_stm32_hal_transfer.Active))
    {
        return;
    }

    if (hst7789_spi_stm32_hal_transfer.RemainingPixels == 0u)
    {
        st7789_spi_stm32_hal_finish_transfer(ST7789_PORT_OK);
        return;
    }

    status = st7789_spi_stm32_hal_start_next_chunk();
    if (status != ST7789_PORT_OK)
    {
        st7789_spi_stm32_hal_finish_transfer(status);
    }
}

/**
 * @brief  响应 HAL SPI 错误回调并终止当前异步 RAMWR 事务。
 * @param  hspi HAL 传入的 SPI Handle。
 * @note   错误时不尝试在 ISR 中重传；ST7789 Device 会释放 CS、进入 ERROR，
 *         再由上层已注册的轻量回调唤醒所属任务决定后续策略。
 */
static void st7789_spi_stm32_hal_error(SPI_HandleTypeDef *hspi)
{
    if ((hst7789_spi_stm32_hal_adapter != NULL) &&
        (hspi == hst7789_spi_stm32_hal_adapter->SPIHandle) &&
        hst7789_spi_stm32_hal_transfer.Active)
    {
        st7789_spi_stm32_hal_finish_transfer(ST7789_PORT_ERROR);
    }
}

/**
  * @brief  通过当前 Adapter 控制 LCD 的片选有效或释放。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  asserted true 选中 LCD，false 释放 LCD。
  * @retval None
  */
static void st7789_spi_stm32_hal_set_chip_select(void *context, bool asserted)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    HAL_GPIO_WritePin(adapter->ChipSelectPort,
                      adapter->ChipSelectPin,
                      asserted
                          ? adapter->ChipSelectActiveState
                          : st7789_spi_stm32_hal_inactive_state(
                                adapter->ChipSelectActiveState));
}

/**
  * @brief  通过当前 Adapter 切换 LCD 的命令或数据模式。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  data_mode true 表示参数/像素数据，false 表示命令字节。
  * @retval None
  */
static void st7789_spi_stm32_hal_set_data_mode(void *context, bool data_mode)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    HAL_GPIO_WritePin(adapter->DataCommandPort,
                      adapter->DataCommandPin,
                      data_mode
                          ? st7789_spi_stm32_hal_inactive_state(adapter->CommandState)
                          : adapter->CommandState);
}

/**
  * @brief  通过当前 Adapter 控制 LCD 的硬件复位有效或释放。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  asserted true 使 RESET 有效，false 释放 RESET。
  * @retval None
  */
static void st7789_spi_stm32_hal_set_reset(void *context, bool asserted)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    HAL_GPIO_WritePin(adapter->ResetPort,
                      adapter->ResetPin,
                      asserted
                          ? adapter->ResetAssertState
                          : st7789_spi_stm32_hal_inactive_state(
                                adapter->ResetAssertState));
}

/**
 * @brief  切换到 8-bit 串行帧后同步写入 ST7789 命令或命令参数。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  data 待发送字节。
  * @param  length 待发送字节数。
  * @retval ST7789_PortStatusTypeDef 归一化后的 HAL SPI 结果。
  */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_write(
    void *context,
    const uint8_t *data,
    uint32_t length)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) ||
        (adapter->SPIHandle == NULL) ||
        (data == NULL) ||
        (length == 0u) ||
        (length > UINT16_MAX))
    {
        return ST7789_PORT_ERROR;
    }

    ST7789_PortStatusTypeDef status = st7789_spi_stm32_hal_select_data_size(
        adapter,
        SPI_DATASIZE_8BIT);

    if (status != ST7789_PORT_OK)
    {
        return status;
    }

    return st7789_spi_stm32_hal_status((int32_t)HAL_SPI_Transmit(adapter->SPIHandle,
                                                                    data,
                                                                    (uint16_t)length,
                                                                    adapter->TimeoutMs));
}

/**
 * @brief  切换到 8-bit 串行帧后发送 dummy 字节并采样 LCD SDO 返回位流。
  * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
  * @param  transmit_data 用于产生读时钟的字节。
  * @param  receive_data 接收 SDO 位流的调用者缓冲区。
  * @param  length 全双工传输的字节数。
  * @retval ST7789_PortStatusTypeDef 归一化后的 HAL SPI 结果。
  */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_transfer(
    void *context,
    const uint8_t *transmit_data,
    uint8_t *receive_data,
    uint32_t length)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) ||
        (adapter->SPIHandle == NULL) ||
        (transmit_data == NULL) ||
        (receive_data == NULL) ||
        (length == 0u) ||
        (length > UINT16_MAX))
    {
        return ST7789_PORT_ERROR;
    }

    ST7789_PortStatusTypeDef status = st7789_spi_stm32_hal_select_data_size(
        adapter,
        SPI_DATASIZE_8BIT);

    if (status != ST7789_PORT_OK)
    {
        return status;
    }

    return st7789_spi_stm32_hal_status(
        (int32_t)HAL_SPI_TransmitReceive(adapter->SPIHandle,
                                          transmit_data,
                                          receive_data,
                                          (uint16_t)length,
                                          adapter->TimeoutMs));
}

/**
 * @brief  注册 ST7789 Device 的异步像素传输完成回调并占有当前 SPI Handle。
 * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
 * @param  callback Adapter 在 SPI ISR 中调用的 Device 回调。
 * @param  callback_context 原样传给 callback 的 Device Handle。
 * @retval ST7789_PORT_OK HAL 回调与 Adapter 归属已安装。
 * @retval ST7789_PORT_BUSY 已有另一 Adapter 占有当前唯一回调归属，或传输在飞。
 * @retval ST7789_PORT_ERROR 参数、HAL 状态或回调注册失败。
 */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_set_transfer_callback(
    void *context,
    ST7789_PortTransferCallback_t callback,
    void *callback_context)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;

    if ((adapter == NULL) || (adapter->SPIHandle == NULL) || (callback == NULL))
    {
        return ST7789_PORT_ERROR;
    }

    if (hst7789_spi_stm32_hal_transfer.Active ||
        ((hst7789_spi_stm32_hal_adapter != NULL) &&
         (hst7789_spi_stm32_hal_adapter != adapter)))
    {
        return ST7789_PORT_BUSY;
    }

    if (HAL_SPI_RegisterCallback(adapter->SPIHandle,
                                 HAL_SPI_TX_COMPLETE_CB_ID,
                                 st7789_spi_stm32_hal_tx_complete) != HAL_OK)
    {
        return ST7789_PORT_ERROR;
    }

    if (HAL_SPI_RegisterCallback(adapter->SPIHandle,
                                 HAL_SPI_ERROR_CB_ID,
                                 st7789_spi_stm32_hal_error) != HAL_OK)
    {
        (void)HAL_SPI_UnRegisterCallback(adapter->SPIHandle,
                                         HAL_SPI_TX_COMPLETE_CB_ID);
        return ST7789_PORT_ERROR;
    }

    hst7789_spi_stm32_hal_adapter = adapter;
    hst7789_spi_stm32_hal_transfer.Callback = callback;
    hst7789_spi_stm32_hal_transfer.CallbackContext = callback_context;
    __DMB();
    return ST7789_PORT_OK;
}

/**
 * @brief  切换到 16-bit 串行帧后启动自动分块续传的 RGB565 SPI DMA 写入。
 * @param  context 必须指向 ST7789_SPI_STM32HALAdapterTypeDef。
 * @param  data 已完整写入、32 字节对齐的 RGB565 像素数据首地址。
 * @param  length 像素数据总字节数，必须为 RGB565 像素大小的整数倍。
 * @retval ST7789_PORT_OK 首块 DMA 已启动。
 * @retval ST7789_PORT_BUSY 已有 ST7789 SPI DMA 事务在飞。
 * @retval ST7789_PORT_ERROR 参数、Cache 维护或 HAL DMA 启动失败。
 * @note   发送方向只需要 Clean；DMA 读取内存而不会向该缓冲区写入，因此完成后
 *         不应 Invalidate。成功切换到 16-bit 后不会在完成时自动恢复；下一次
 *         命令、命令参数或读操作会在其入口恢复 8-bit。直到最终回调前，调用者
 *         不得修改该内存范围。Cache Clean 会向后补齐至完整 Cache line，调用者
 *         必须拥有补齐后的范围。
 */
static ST7789_PortStatusTypeDef st7789_spi_stm32_hal_start_write(
    void *context,
    const uint8_t *data,
    uint32_t length)
{
    ST7789_SPI_STM32HALAdapterTypeDef *adapter =
        (ST7789_SPI_STM32HALAdapterTypeDef *)context;
    ST7789_PortStatusTypeDef status;

    if ((adapter == NULL) ||
        (adapter->SPIHandle == NULL) ||
        (data == NULL) ||
        (length == 0u) ||
        ((length & 1u) != 0u) ||
        (hst7789_spi_stm32_hal_adapter != adapter) ||
        (hst7789_spi_stm32_hal_transfer.Callback == NULL))
    {
        return ST7789_PORT_ERROR;
    }

    if (hst7789_spi_stm32_hal_transfer.Active)
    {
        return ST7789_PORT_BUSY;
    }

    if (((uintptr_t)data & (sizeof(uint16_t) - 1u)) != 0u)
    {
        return ST7789_PORT_ERROR;
    }

    if ((adapter->SPIHandle->hdmatx == NULL) ||
        (adapter->SPIHandle->hdmatx->Init.PeriphDataAlignment != DMA_PDATAALIGN_HALFWORD) ||
        (adapter->SPIHandle->hdmatx->Init.MemDataAlignment != DMA_MDATAALIGN_HALFWORD))
    {
        return ST7789_PORT_ERROR;
    }

    if (!CortexM7DCache_Clean_Rounded(data, length))
    {
        return ST7789_PORT_ERROR;
    }

    status = st7789_spi_stm32_hal_select_data_size(adapter, SPI_DATASIZE_16BIT);
    if (status != ST7789_PORT_OK)
    {
        return status;
    }

    hst7789_spi_stm32_hal_transfer.Pixels = (const uint16_t *)data;
    hst7789_spi_stm32_hal_transfer.RemainingPixels = length / sizeof(uint16_t);
    hst7789_spi_stm32_hal_transfer.Active = true;
    __DMB();

    status = st7789_spi_stm32_hal_start_next_chunk();
    if (status != ST7789_PORT_OK)
    {
        hst7789_spi_stm32_hal_transfer.Active = false;
        hst7789_spi_stm32_hal_transfer.Pixels = NULL;
        hst7789_spi_stm32_hal_transfer.RemainingPixels = 0u;
        __DMB();
    }

    return status;
}

/**
  * @brief  委托 STM32 HAL 的毫秒延时实现 ST7789 上电与复位等待。
  * @param  context 当前未使用，保留以满足 ST7789_PortDelayMsFunc。
  * @param  delay_ms 等待时间，单位为毫秒。
  * @retval None
  */
static void st7789_spi_stm32_hal_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    HAL_Delay(delay_ms);
}

/** @brief STM32 HAL SPI/GPIO 实现的 ST7789 PortOps 操作表。 */
static const ST7789_PortOpsTypeDef st7789_spi_stm32_hal_ops = {
    .SetChipSelect = st7789_spi_stm32_hal_set_chip_select,
    .SetDataMode = st7789_spi_stm32_hal_set_data_mode,
    .SetReset = st7789_spi_stm32_hal_set_reset,
    .Write = st7789_spi_stm32_hal_write,
    .Transfer = st7789_spi_stm32_hal_transfer,
    .StartWrite = st7789_spi_stm32_hal_start_write,
    .SetTransferCallback = st7789_spi_stm32_hal_set_transfer_callback,
    .DelayMs = st7789_spi_stm32_hal_delay_ms
};

/**
  * @brief  将 STM32 HAL SPI/GPIO Adapter 安装到 ST7789 Device Handle。
  * @param  hst7789 待绑定的 ST7789 Device Handle。
  * @param  adapter 由 Platform 长期持有的具体 SPI/GPIO Context。
  * @retval ST7789_OK PortOps 与 Context 已成对安装。
  * @retval ST7789_ERROR Handle、Context、GPIO、SPI 或超时参数无效。
  * @note   本函数只完成依赖装配，不会改变 CS、D/C、RESET 或启动 SPI 传输。
  */
ST7789_StatusTypeDef ST7789_SPI_STM32HALAdapter_Bind(
    ST7789_HandleTypeDef *hst7789,
    ST7789_SPI_STM32HALAdapterTypeDef *adapter)
{
    if ((hst7789 == NULL) ||
        (adapter == NULL) ||
        (adapter->SPIHandle == NULL) ||
        (adapter->ChipSelectPort == NULL) ||
        (adapter->ChipSelectPin == 0u) ||
        (adapter->DataCommandPort == NULL) ||
        (adapter->DataCommandPin == 0u) ||
        (adapter->ResetPort == NULL) ||
        (adapter->ResetPin == 0u) ||
        (adapter->TimeoutMs == 0u))
    {
        return ST7789_ERROR;
    }

    hst7789->PortOps = &st7789_spi_stm32_hal_ops;
    hst7789->PortContext = adapter;
    return ST7789_OK;
}
