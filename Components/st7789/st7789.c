/**
  ******************************************************************************
  * @file    st7789.c
  * @brief   ST7789 最小复位与 RDDID 读取实现。
  *
  * @details
  *          本 Device 只表达 ST7789 的串行协议与时序，不认识 STM32 HAL、
  *          FreeRTOS、GPIO 端口或本板的 SPI 实例。具体传输和引脚电平由
  *          Adapter 通过 ST7789_PortOpsTypeDef 提供。
  ******************************************************************************
  */

#include "Components/st7789/st7789.h"
#include "Components/st7789/st7789_config.h"

#include <stddef.h>

/**
 * @brief ST7789 纯色填充使用的静态 RGB565 串行数据块。
 * @note  位于静态存储期，避免在 LCD Task 的有限栈中创建大数组。当前 Device
 *        不提供多任务并发保护；同一实例由 Platform LCD 的单一调用上下文串行使用。
 */
static uint8_t st7789_fill_buffer[ST7789_FILL_BUFFER_PIXELS * 2u];

/**
  * @brief  清除最近一次 Device 和端口诊断。
  * @param  hst7789 已完成空指针检查的 ST7789 Handle。
  * @retval None
  */
static void st7789_clear_error(ST7789_HandleTypeDef *hst7789)
{
    hst7789->ErrorCode = ST7789_ERROR_NONE;
    hst7789->LastPortStatus = ST7789_PORT_OK;
}

/**
  * @brief  判断当前 Handle 是否已安装最小读 ID 所需的完整 PortOps。
  * @param  hst7789 待检查的 ST7789 Handle。
  * @retval true PortOps、Context 和全部必要回调均有效。
  * @retval false 绑定不完整。
  */
static bool st7789_is_port_bound(const ST7789_HandleTypeDef *hst7789)
{
    return (hst7789 != NULL) &&
           (hst7789->PortOps != NULL) &&
           (hst7789->PortContext != NULL) &&
           (hst7789->PortOps->SetChipSelect != NULL) &&
           (hst7789->PortOps->SetDataMode != NULL) &&
           (hst7789->PortOps->SetReset != NULL) &&
           (hst7789->PortOps->Write != NULL) &&
           (hst7789->PortOps->Transfer != NULL) &&
           (hst7789->PortOps->DelayMs != NULL);
}

/**
 * @brief  判断当前 Handle 是否已安装异步像素传输所需的 PortOps。
 * @param  hst7789 待检查的 ST7789 Handle。
 * @retval true 同步基础 PortOps 与异步写入/回调 PortOps 均有效。
 * @retval false 绑定不完整，不能启动 DMA 像素写入。
 */
static bool st7789_is_async_port_bound(const ST7789_HandleTypeDef *hst7789)
{
    return st7789_is_port_bound(hst7789) &&
           (hst7789->PortOps->StartWrite != NULL) &&
           (hst7789->PortOps->SetTransferCallback != NULL);
}

/**
  * @brief  统一保存失败阶段、端口原因和后续 Device 状态。
  * @param  hst7789 已完成空指针检查的 ST7789 Handle。
  * @param  error 本次失败所处的 Device 语义阶段。
  * @param  port_status Adapter 归一化后的端口结果。
  * @param  next_state 失败后写入的生命周期状态。
  * @retval ST7789_ERROR。
  */
static ST7789_StatusTypeDef st7789_fail(
    ST7789_HandleTypeDef *hst7789,
    ST7789_ErrorTypeDef error,
    ST7789_PortStatusTypeDef port_status,
    ST7789_StateTypeDef next_state)
{
    hst7789->ErrorCode = error;
    hst7789->LastPortStatus = port_status;
    hst7789->State = next_state;
    return ST7789_ERROR;
}

/**
 * @brief  接收 Adapter 从 ISR 发布的异步像素传输最终结果。
 * @param  port_status Adapter 归一化后的最终传输结果。
 * @param  context 注册时绑定的 ST7789 Handle。
 * @note   本函数运行于 Adapter 的 HAL SPI 回调上下文。它只结束本次 ST7789
 *         事务、恢复 Device 状态并转交调用者的轻量回调；不得在此处添加日志、
 *         延时或任何 GUI 业务。
 */
static void st7789_transfer_complete(ST7789_PortStatusTypeDef port_status,
                                     void *context)
{
    ST7789_HandleTypeDef *hst7789 = (ST7789_HandleTypeDef *)context;
    ST7789_TransferCallback_t callback;
    void *callback_context;
    ST7789_StatusTypeDef status;

    if ((hst7789 == NULL) || (hst7789->PortOps == NULL))
    {
        return;
    }

    /* RAMWR 数据事务跨越全部 DMA 分块，只有最终 EOT 后才能释放 CS。 */
    hst7789->PortOps->SetChipSelect(hst7789->PortContext, false);
    callback = hst7789->TransferCallback;
    callback_context = hst7789->TransferContext;

    if (port_status == ST7789_PORT_OK)
    {
        hst7789->ErrorCode = ST7789_ERROR_NONE;
        hst7789->LastPortStatus = ST7789_PORT_OK;
        hst7789->State = ST7789_STATE_READY;
        status = ST7789_OK;
    }
    else
    {
        hst7789->ErrorCode = ST7789_ERROR_WRITE_DATA;
        hst7789->LastPortStatus = port_status;
        hst7789->State = ST7789_STATE_ERROR;
        status = ST7789_ERROR;
    }

    if (callback != NULL)
    {
        callback(status, callback_context);
    }
}

/**
 * @brief  在保持 CS 有效的事务中写入一字节 ST7789 命令。
 * @param  hst7789 已绑定且已由调用者选中的 Device Handle。
 * @param  command 待写入的命令字节。
 * @retval ST7789_PortStatusTypeDef Adapter 的写入结果。
 */
static ST7789_PortStatusTypeDef st7789_write_command(
    ST7789_HandleTypeDef *hst7789,
    uint8_t command)
{
    hst7789->PortOps->SetDataMode(hst7789->PortContext, false);
    return hst7789->PortOps->Write(hst7789->PortContext, &command, 1u);
}

/**
 * @brief  在保持 CS 有效的事务中写入 ST7789 命令参数字节。
 * @param  hst7789 已绑定且已由调用者选中的 Device Handle。
 * @param  data 待写入的命令参数字节。
 * @param  length 数据长度，单位为字节。
 * @retval ST7789_PortStatusTypeDef Adapter 的写入结果。
 */
static ST7789_PortStatusTypeDef st7789_write_data(
    ST7789_HandleTypeDef *hst7789,
    const uint8_t *data,
    uint32_t length)
{
    hst7789->PortOps->SetDataMode(hst7789->PortContext, true);
    return hst7789->PortOps->Write(hst7789->PortContext, data, length);
}

/**
 * @brief  向已选中的 ST7789 依次写入命令和可选参数。
 * @param  hst7789 已绑定且已由调用者选中的 Device Handle。
 * @param  command 待写入的命令字节。
 * @param  data 可选参数首地址；参数长度为零时可以为 NULL。
 * @param  length 参数长度，单位为字节。
 * @param  error 输出实际失败阶段。
 * @retval ST7789_PortStatusTypeDef Adapter 的写入结果。
 */
static ST7789_PortStatusTypeDef st7789_write_command_with_data(
    ST7789_HandleTypeDef *hst7789,
    uint8_t command,
    const uint8_t *data,
    uint32_t length,
    ST7789_ErrorTypeDef *error)
{
    ST7789_PortStatusTypeDef port_status;

    port_status = st7789_write_command(hst7789, command);
    if (port_status != ST7789_PORT_OK)
    {
        *error = ST7789_ERROR_WRITE_COMMAND;
        return port_status;
    }

    if (length == 0u)
    {
        return ST7789_PORT_OK;
    }

    port_status = st7789_write_data(hst7789, data, length);
    if (port_status != ST7789_PORT_OK)
    {
        *error = ST7789_ERROR_WRITE_DATA;
    }

    return port_status;
}

/**
 * @brief  设置包含起止坐标的 ST7789 列/行地址窗口。
 * @param  hst7789 已绑定且已由调用者选中的 Device Handle。
 * @param  x_start 矩形左边界，单位为像素。
 * @param  y_start 矩形上边界，单位为像素。
 * @param  x_end 矩形右边界，单位为像素，包含该点。
 * @param  y_end 矩形下边界，单位为像素，包含该点。
 * @param  error 输出实际失败阶段。
 * @note   CASET 与 RASET 的四个参数分别是 XStart、XEnd、YStart、YEnd；
 *         RAMWR 接受后，控制器会在这个闭区间内自动递增地址。
 * @retval ST7789_PortStatusTypeDef Adapter 的写入结果。
 */
static ST7789_PortStatusTypeDef st7789_set_address_window(
    ST7789_HandleTypeDef *hst7789,
    uint16_t x_start,
    uint16_t y_start,
    uint16_t x_end,
    uint16_t y_end,
    ST7789_ErrorTypeDef *error)
{
    const uint8_t column_data[4] = {
        (uint8_t)(x_start >> 8u),
        (uint8_t)x_start,
        (uint8_t)(x_end >> 8u),
        (uint8_t)x_end
    };
    const uint8_t row_data[4] = {
        (uint8_t)(y_start >> 8u),
        (uint8_t)y_start,
        (uint8_t)(y_end >> 8u),
        (uint8_t)y_end
    };
    ST7789_PortStatusTypeDef port_status;

    port_status = st7789_write_command_with_data(hst7789,
                                                   ST7789_COMMAND_CASET,
                                                   column_data,
                                                   sizeof(column_data),
                                                   error);
    if (port_status != ST7789_PORT_OK)
    {
        return port_status;
    }

    return st7789_write_command_with_data(hst7789,
                                           ST7789_COMMAND_RASET,
                                           row_data,
                                           sizeof(row_data),
                                           error);
}

/**
 * @brief  判断矩形闭区间是否完全位于当前 LCD 可见区域内。
 * @param  x_start 矩形左边界。
 * @param  y_start 矩形上边界。
 * @param  x_end 矩形右边界。
 * @param  y_end 矩形下边界。
 * @retval true 坐标顺序合法且没有越过可见区域。
 * @retval false 坐标反向或超出显示边界。
 */
static bool st7789_is_valid_rect(uint16_t x_start,
                                  uint16_t y_start,
                                  uint16_t x_end,
                                  uint16_t y_end)
{
    return (x_start <= x_end) &&
           (y_start <= y_end) &&
           (x_end < ST7789_WIDTH) &&
           (y_end < ST7789_HEIGHT);
}

/**
  * @brief  对齐 RDDID 的单个 dummy clock，并恢复 24 位 ID 的字节边界。
  * @param  raw 以 4 个 8 位 SPI 帧接收的原始位流。
  * @param  id 接收解码后 3 字节 ID 的输出结构。
  * @note   ST7789 在命令结束后先输出一个 dummy clock，直接按字节读取会使
  *         D23..D0 左移一位。第 4 个接收字节用于补齐 ID3 的最低位。
  * @retval None
  */
static void st7789_decode_rddid(const uint8_t raw[ST7789_RDDID_TRANSFER_LENGTH],
                                ST7789_IDTypeDef *id)
{
    id->ID1 = (uint8_t)((raw[0] << 1u) | (raw[1] >> 7u));
    id->ID2 = (uint8_t)((raw[1] << 1u) | (raw[2] >> 7u));
    id->ID3 = (uint8_t)((raw[2] << 1u) | (raw[3] >> 7u));
}

/**
  * @brief  通过 RESET 引脚执行一次 ST7789 硬件复位。
  * @param  hst7789 已绑定的 ST7789 Handle。
  * @note   Reset 低电平至少保持 10 us；这里使用 10 ms 留出板级电源和 GPIO
  *         建立余量。释放后至少等待 120 ms，确保控制器从 NVM 装载设置。
  * @retval ST7789_OK 复位时序完成。
  */
ST7789_StatusTypeDef ST7789_Init(ST7789_HandleTypeDef *hst7789)
{
    if (hst7789 == NULL)
    {
        return ST7789_ERROR;
    }

    if (!st7789_is_port_bound(hst7789))
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_PORT_NOT_BOUND,
                           ST7789_PORT_ERROR,
                           ST7789_STATE_ERROR);
    }

    hst7789->State = ST7789_STATE_BUSY;
    st7789_clear_error(hst7789);

    /* 传输空闲态必须释放 CS，并让 D/C 保持为命令电平。 */
    hst7789->PortOps->SetChipSelect(hst7789->PortContext, false);
    hst7789->PortOps->SetDataMode(hst7789->PortContext, false);
    hst7789->PortOps->SetReset(hst7789->PortContext, true);
    hst7789->PortOps->DelayMs(hst7789->PortContext, ST7789_RESET_ASSERT_DELAY_MS);
    hst7789->PortOps->SetReset(hst7789->PortContext, false);
    hst7789->PortOps->DelayMs(hst7789->PortContext, ST7789_RESET_RELEASE_DELAY_MS);

    hst7789->State = ST7789_STATE_READY;
    return ST7789_OK;
}

/**
  * @brief  发送 RDDID 命令并读取 ST7789 返回的 24 位显示标识。
  * @param  hst7789 已完成硬件复位的 ST7789 Handle。
  * @param  id 接收 3 个 ID 字节的调用者结构。
  * @note   串行读过程保持 CS 有效：先以 D/C=0 写入 0x04，再以 D/C=1 发送
  *         4 个 dummy 字节产生时钟并读取 SDO。整个过程是同步阻塞操作，适合
  *         当前最小诊断；后续帧缓冲传输应另行接入 DMA。
  * @retval ST7789_OK 读取并完成 dummy-bit 对齐。
  * @retval ST7789_ERROR 参数、绑定、状态或 SPI 传输失败。
  */
ST7789_StatusTypeDef ST7789_ReadID(ST7789_HandleTypeDef *hst7789,
                                   ST7789_IDTypeDef *id)
{
    const uint8_t command = ST7789_COMMAND_RDDID;
    const uint8_t dummy_transmit[ST7789_RDDID_TRANSFER_LENGTH] = {0u};
    uint8_t raw_receive[ST7789_RDDID_TRANSFER_LENGTH] = {0u};
    ST7789_PortStatusTypeDef port_status;

    if ((hst7789 == NULL) || (id == NULL))
    {
        return ST7789_ERROR;
    }

    if (!st7789_is_port_bound(hst7789))
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_PORT_NOT_BOUND,
                           ST7789_PORT_ERROR,
                           ST7789_STATE_ERROR);
    }

    if (hst7789->State != ST7789_STATE_READY)
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_NOT_READY,
                           ST7789_PORT_OK,
                           hst7789->State);
    }

    st7789_clear_error(hst7789);
    hst7789->State = ST7789_STATE_BUSY;

    hst7789->PortOps->SetChipSelect(hst7789->PortContext, true);
    hst7789->PortOps->SetDataMode(hst7789->PortContext, false);

    port_status = hst7789->PortOps->Write(hst7789->PortContext, &command, 1u);
    if (port_status != ST7789_PORT_OK)
    {
        hst7789->PortOps->SetChipSelect(hst7789->PortContext, false);
        return st7789_fail(hst7789,
                           ST7789_ERROR_WRITE_COMMAND,
                           port_status,
                           (port_status == ST7789_PORT_BUSY)
                               ? ST7789_STATE_READY
                               : ST7789_STATE_ERROR);
    }

    hst7789->PortOps->SetDataMode(hst7789->PortContext, true);
    port_status = hst7789->PortOps->Transfer(hst7789->PortContext,
                                              dummy_transmit,
                                              raw_receive,
                                              ST7789_RDDID_TRANSFER_LENGTH);
    hst7789->PortOps->SetChipSelect(hst7789->PortContext, false);

    if (port_status != ST7789_PORT_OK)
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_READ_ID,
                           port_status,
                           (port_status == ST7789_PORT_BUSY)
                               ? ST7789_STATE_READY
                               : ST7789_STATE_ERROR);
    }

    st7789_decode_rddid(raw_receive, id);
    hst7789->State = ST7789_STATE_READY;
    return ST7789_OK;
}

/**
 * @brief  将已完成硬件复位的 ST7789 配置为 RGB565 正常显示模式。
 * @param  hst7789 已完成 `ST7789_Init()` 的 ST7789 Handle。
 * @note   依次退出休眠、设置 RGB565、设置左上 RGB 扫描方向、开启 IPS 所需的
 *         反相驱动、切入正常显示并打开输出。该函数不会写入任何用户像素，也不
 *         控制本板背光；背光属于 Platform 的 PCB 资源。
 * @retval ST7789_OK 显示控制器已可接受 `RAMWR` 像素写入。
 * @retval ST7789_ERROR 参数、绑定、状态或任一命令事务失败。
 */
ST7789_StatusTypeDef ST7789_DisplayInit(ST7789_HandleTypeDef *hst7789)
{
    const uint8_t color_mode = ST7789_COLMOD_RGB565;
    const uint8_t memory_access_control = ST7789_MADCTL_RGB_TOP_LEFT;
    const uint8_t frame_rate_control = ST7789_FRCTRL2_60HZ;
    ST7789_ErrorTypeDef error = ST7789_ERROR_NONE;
    ST7789_PortStatusTypeDef port_status;

    if (hst7789 == NULL)
    {
        return ST7789_ERROR;
    }

    if (!st7789_is_port_bound(hst7789))
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_PORT_NOT_BOUND,
                           ST7789_PORT_ERROR,
                           ST7789_STATE_ERROR);
    }

    if (hst7789->State != ST7789_STATE_READY)
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_NOT_READY,
                           ST7789_PORT_OK,
                           hst7789->State);
    }

    st7789_clear_error(hst7789);
    hst7789->State = ST7789_STATE_BUSY;
    hst7789->PortOps->SetChipSelect(hst7789->PortContext, true);

    port_status = st7789_write_command_with_data(hst7789,
                                                   ST7789_COMMAND_SLPOUT,
                                                   NULL,
                                                   0u,
                                                   &error);
    if (port_status == ST7789_PORT_OK)
    {
        hst7789->PortOps->DelayMs(hst7789->PortContext, ST7789_SLEEP_OUT_DELAY_MS);
        port_status = st7789_write_command_with_data(hst7789,
                                                       ST7789_COMMAND_COLMOD,
                                                       &color_mode,
                                                       1u,
                                                       &error);
    }

    if (port_status == ST7789_PORT_OK)
    {
        port_status = st7789_write_command_with_data(hst7789,
                                                       ST7789_COMMAND_MADCTL,
                                                       &memory_access_control,
                                                       1u,
                                                       &error);
    }

    if (port_status == ST7789_PORT_OK)
    {
        port_status = st7789_write_command_with_data(hst7789,
                                                       ST7789_COMMAND_FRCTRL2,
                                                       &frame_rate_control,
                                                       1u,
                                                       &error);
    }

    if (port_status == ST7789_PORT_OK)
    {
        port_status = st7789_write_command_with_data(hst7789,
                                                       ST7789_COMMAND_INVON,
                                                       NULL,
                                                       0u,
                                                       &error);
    }

    if (port_status == ST7789_PORT_OK)
    {
        port_status = st7789_write_command_with_data(hst7789,
                                                       ST7789_COMMAND_NORON,
                                                       NULL,
                                                       0u,
                                                       &error);
    }

    if (port_status == ST7789_PORT_OK)
    {
        hst7789->PortOps->DelayMs(hst7789->PortContext, ST7789_NORMAL_MODE_DELAY_MS);
        port_status = st7789_write_command_with_data(hst7789,
                                                       ST7789_COMMAND_DISPON,
                                                       NULL,
                                                       0u,
                                                       &error);
    }

    hst7789->PortOps->SetChipSelect(hst7789->PortContext, false);

    if (port_status != ST7789_PORT_OK)
    {
        return st7789_fail(hst7789,
                           error,
                           port_status,
                           (port_status == ST7789_PORT_BUSY)
                               ? ST7789_STATE_READY
                               : ST7789_STATE_ERROR);
    }

    hst7789->PortOps->DelayMs(hst7789->PortContext, ST7789_DISPLAY_ON_DELAY_MS);
    hst7789->State = ST7789_STATE_READY;
    return ST7789_OK;
}

/**
 * @brief  向 ST7789 的一个包含边界的矩形区域连续写入同一 RGB565 颜色。
 * @param  hst7789 已完成显示初始化的 ST7789 Handle。
 * @param  x_start 矩形左边界，范围为 `0` 至 `ST7789_WIDTH - 1`。
 * @param  y_start 矩形上边界，范围为 `0` 至 `ST7789_HEIGHT - 1`。
 * @param  x_end 矩形右边界，包含该像素。
 * @param  y_end 矩形下边界，包含该像素。
 * @param  color 按 `RRRRRGGGGGGBBBBB` 编码的 RGB565 颜色。
 * @note   CASET 写入 XStart/XEnd，RASET 写入 YStart/YEnd，随后只发送一次
 *         RAMWR；ST7789 会在窗口闭区间内自动递增地址。像素数据按高字节、
 *         低字节发送，符合 4-line SPI 的串行 MSB-first 格式。
 * @retval ST7789_OK 整个矩形已写入 Display RAM。
 * @retval ST7789_ERROR 参数、状态或 SPI 传输失败。
 */
ST7789_StatusTypeDef ST7789_FillRect(ST7789_HandleTypeDef *hst7789,
                                     uint16_t x_start,
                                     uint16_t y_start,
                                     uint16_t x_end,
                                     uint16_t y_end,
                                     uint16_t color)
{
    uint32_t remaining_pixels;
    uint32_t chunk_pixels;
    ST7789_ErrorTypeDef error = ST7789_ERROR_NONE;
    ST7789_PortStatusTypeDef port_status;

    if (hst7789 == NULL)
    {
        return ST7789_ERROR;
    }

    if (!st7789_is_port_bound(hst7789))
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_PORT_NOT_BOUND,
                           ST7789_PORT_ERROR,
                           ST7789_STATE_ERROR);
    }

    if (hst7789->State != ST7789_STATE_READY)
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_NOT_READY,
                           ST7789_PORT_OK,
                           hst7789->State);
    }

    if (!st7789_is_valid_rect(x_start, y_start, x_end, y_end))
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_INVALID_PARAM,
                           ST7789_PORT_OK,
                           ST7789_STATE_READY);
    }

    for (uint32_t index = 0u; index < ST7789_FILL_BUFFER_PIXELS; ++index)
    {
        st7789_fill_buffer[index * 2u] = (uint8_t)(color >> 8u);
        st7789_fill_buffer[(index * 2u) + 1u] = (uint8_t)color;
    }

    remaining_pixels = ((uint32_t)x_end - x_start + 1u) *
                       ((uint32_t)y_end - y_start + 1u);

    st7789_clear_error(hst7789);
    hst7789->State = ST7789_STATE_BUSY;
    hst7789->PortOps->SetChipSelect(hst7789->PortContext, true);

    port_status = st7789_set_address_window(hst7789,
                                             x_start,
                                             y_start,
                                             x_end,
                                             y_end,
                                             &error);
    if (port_status == ST7789_PORT_OK)
    {
        port_status = st7789_write_command(hst7789, ST7789_COMMAND_RAMWR);
        if (port_status != ST7789_PORT_OK)
        {
            error = ST7789_ERROR_WRITE_COMMAND;
        }
    }

    while ((port_status == ST7789_PORT_OK) && (remaining_pixels > 0u))
    {
        chunk_pixels = (remaining_pixels > ST7789_FILL_BUFFER_PIXELS)
                           ? ST7789_FILL_BUFFER_PIXELS
                           : remaining_pixels;
        port_status = st7789_write_data(hst7789,
                                         st7789_fill_buffer,
                                         chunk_pixels * 2u);
        if (port_status != ST7789_PORT_OK)
        {
            error = ST7789_ERROR_WRITE_DATA;
        }

        remaining_pixels -= chunk_pixels;
    }

    hst7789->PortOps->SetChipSelect(hst7789->PortContext, false);

    if (port_status != ST7789_PORT_OK)
    {
        return st7789_fail(hst7789,
                           error,
                           port_status,
                           (port_status == ST7789_PORT_BUSY)
                               ? ST7789_STATE_READY
                               : ST7789_STATE_ERROR);
    }

    hst7789->State = ST7789_STATE_READY;
    return ST7789_OK;
}

/**
 * @brief  向 ST7789 的一个坐标点写入 RGB565 像素。
 * @param  hst7789 已完成显示初始化的 ST7789 Handle。
 * @param  x 像素 X 坐标。
 * @param  y 像素 Y 坐标。
 * @param  color RGB565 颜色。
 * @note   画点是 `ST7789_FillRect()` 的退化情况：XStart=XEnd 且 YStart=YEnd。
 *         因而窗口设置、边界检查、CS 生命周期和错误处理只有一份 Implementation。
 * @retval ST7789_OK 像素已写入 Display RAM。
 * @retval ST7789_ERROR 坐标非法、状态不允许或 SPI 写入失败。
 */
ST7789_StatusTypeDef ST7789_DrawPixel(ST7789_HandleTypeDef *hst7789,
                                      uint16_t x,
                                      uint16_t y,
                                      uint16_t color)
{
    return ST7789_FillRect(hst7789, x, y, x, y, color);
}

/**
 * @brief  设置 ST7789 异步像素传输完成时调用的唯一订阅者。
 * @param  hst7789 已绑定的 ST7789 Handle。
 * @param  callback 在 Adapter ISR 上下文调用的轻量回调，不能为空。
 * @param  context 原样传给 callback 的调用者上下文，可为 NULL。
 * @retval ST7789_OK 回调已同时安装到 Device 与已绑定 Adapter。
 * @retval ST7789_BUSY Device 正在传输，不能替换订阅者。
 * @retval ST7789_ERROR 参数、PortOps 或 HAL 回调注册失败。
 * @note   回调只能发布任务通知或执行其他常数时间操作。它不应直接调用 LVGL、
 *         记录日志或启动下一帧绘制。
 */
ST7789_StatusTypeDef ST7789_SetTransferCallback(
    ST7789_HandleTypeDef *hst7789,
    ST7789_TransferCallback_t callback,
    void *context)
{
    ST7789_PortStatusTypeDef port_status;

    if ((hst7789 == NULL) || (callback == NULL))
    {
        return ST7789_ERROR;
    }

    if (!st7789_is_async_port_bound(hst7789))
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_TRANSFER_CALLBACK,
                           ST7789_PORT_ERROR,
                           ST7789_STATE_ERROR);
    }

    if (hst7789->State == ST7789_STATE_BUSY)
    {
        return ST7789_BUSY;
    }

    port_status = hst7789->PortOps->SetTransferCallback(
        hst7789->PortContext,
        st7789_transfer_complete,
        hst7789);
    if (port_status != ST7789_PORT_OK)
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_TRANSFER_CALLBACK,
                           port_status,
                           (port_status == ST7789_PORT_BUSY)
                               ? ST7789_STATE_READY
                               : ST7789_STATE_ERROR);
    }

    hst7789->TransferCallback = callback;
    hst7789->TransferContext = context;
    return ST7789_OK;
}

/**
 * @brief  设置一个 ST7789 地址窗口并异步写入其完整 RGB565 像素数据。
 * @param  hst7789 已完成显示初始化且已注册传输回调的 ST7789 Handle。
 * @param  x_start 矩形左边界，范围为 `0` 至 `ST7789_WIDTH - 1`。
 * @param  y_start 矩形上边界，范围为 `0` 至 `ST7789_HEIGHT - 1`。
 * @param  x_end 矩形右边界，包含该像素。
 * @param  y_end 矩形下边界，包含该像素。
 * @param  pixels 按 RGB565 连续存放的调用者像素缓冲区。
 * @retval ST7789_OK CASET、RASET、RAMWR 已完成，像素 DMA 已开始。
 * @retval ST7789_BUSY 仍有先前异步像素传输在飞。
 * @retval ST7789_ERROR 参数、状态、命令事务或 DMA 启动失败。
 * @note   成功返回不表示像素已全部移出 MOSI；调用者必须等待
 *         `ST7789_SetTransferCallback()` 设置的完成回调。pixels 在回调到达前
 *         必须保持有效且不可修改。
 */
ST7789_StatusTypeDef ST7789_StartWrite(ST7789_HandleTypeDef *hst7789,
                                       uint16_t x_start,
                                       uint16_t y_start,
                                       uint16_t x_end,
                                       uint16_t y_end,
                                       const uint16_t *pixels)
{
    ST7789_ErrorTypeDef error = ST7789_ERROR_NONE;
    ST7789_PortStatusTypeDef port_status;
    uint32_t pixel_count;
    uint32_t byte_count;

    if ((hst7789 == NULL) || (pixels == NULL))
    {
        return ST7789_ERROR;
    }

    if (!st7789_is_async_port_bound(hst7789))
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_PORT_NOT_BOUND,
                           ST7789_PORT_ERROR,
                           ST7789_STATE_ERROR);
    }

    if (hst7789->State == ST7789_STATE_BUSY)
    {
        return ST7789_BUSY;
    }

    if (hst7789->State != ST7789_STATE_READY)
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_NOT_READY,
                           ST7789_PORT_OK,
                           hst7789->State);
    }

    if (!st7789_is_valid_rect(x_start, y_start, x_end, y_end))
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_INVALID_PARAM,
                           ST7789_PORT_OK,
                           ST7789_STATE_READY);
    }

    if (hst7789->TransferCallback == NULL)
    {
        return st7789_fail(hst7789,
                           ST7789_ERROR_TRANSFER_CALLBACK,
                           ST7789_PORT_ERROR,
                           ST7789_STATE_READY);
    }

    pixel_count = ((uint32_t)x_end - x_start + 1u) *
                  ((uint32_t)y_end - y_start + 1u);
    byte_count = pixel_count * sizeof(*pixels);

    st7789_clear_error(hst7789);
    hst7789->State = ST7789_STATE_BUSY;
    hst7789->PortOps->SetChipSelect(hst7789->PortContext, true);

    port_status = st7789_set_address_window(hst7789,
                                             x_start,
                                             y_start,
                                             x_end,
                                             y_end,
                                             &error);
    if (port_status == ST7789_PORT_OK)
    {
        port_status = st7789_write_command(hst7789, ST7789_COMMAND_RAMWR);
        if (port_status != ST7789_PORT_OK)
        {
            error = ST7789_ERROR_WRITE_COMMAND;
        }
    }

    if (port_status == ST7789_PORT_OK)
    {
        hst7789->PortOps->SetDataMode(hst7789->PortContext, true);
        port_status = hst7789->PortOps->StartWrite(
            hst7789->PortContext,
            (const uint8_t *)pixels,
            byte_count);
        if (port_status != ST7789_PORT_OK)
        {
            error = ST7789_ERROR_START_TRANSFER;
        }
    }

    if (port_status != ST7789_PORT_OK)
    {
        hst7789->PortOps->SetChipSelect(hst7789->PortContext, false);
        return st7789_fail(hst7789,
                           error,
                           port_status,
                           (port_status == ST7789_PORT_BUSY)
                               ? ST7789_STATE_READY
                               : ST7789_STATE_ERROR);
    }

    return ST7789_OK;
}
