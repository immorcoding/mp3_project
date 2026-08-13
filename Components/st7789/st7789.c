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

#include <stddef.h>

/** @brief ST7789 的 Read Display Identification 命令。 */
#define ST7789_COMMAND_RDDID                 0x04u

/** @brief 硬件复位低电平保持时间，单位为毫秒。 */
#define ST7789_RESET_ASSERT_DELAY_MS          10u

/** @brief 释放 RESET 后等待 NVM 设置装载的时间，单位为毫秒。 */
#define ST7789_RESET_RELEASE_DELAY_MS         120u

/** @brief RDDID 的 24 位返回数据需要额外产生的完整串行字节数。 */
#define ST7789_RDDID_TRANSFER_LENGTH          4u

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
