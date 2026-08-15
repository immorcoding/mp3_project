/**
  ******************************************************************************
  * @file    ft6x36.c
  * @brief   FT6X36 电容触摸控制器 Device 实现。
  *
  * @details
  *          本 Module 封装 FT6X36 的复位时序、I2C 就绪检查和芯片标识寄存器
  *          读取。它不认识 HAL I2C、具体 GPIO、FreeRTOS 或 LCD/LVGL；这些
  *          外部能力均通过 PortOps 由 Platform 绑定。
  ******************************************************************************
  */

#include "Components/ft6x36/ft6x36.h"
#include "Components/ft6x36/ft6x36_config.h"

#include <stddef.h>

/**
  * @brief  记录一次 Port 操作失败并把 Device 置为错误状态。
  * @param  hft6x36 已绑定的 FT6X36 Handle。
  * @param  error 本次失败对应的 Device 语义阶段。
  * @param  port_status Adapter 返回的归一化底层状态。
  * @param  register_address 失败关联的寄存器；没有寄存器时传入 0。
  * @retval FT6X36_ERROR。
  * @note   错误状态要求调用者重新执行 FT6X36_Init()，避免在未确认复位和 I2C
  *         状态的情况下继续读取寄存器。
  */
static FT6X36_StatusTypeDef ft6x36_record_port_failure(
    FT6X36_HandleTypeDef *hft6x36,
    FT6X36_ErrorTypeDef error,
    FT6X36_PortStatusTypeDef port_status,
    uint8_t register_address)
{
    hft6x36->ErrorCode = error;
    hft6x36->LastPortStatus = port_status;
    hft6x36->LastFailedRegister = register_address;
    hft6x36->State = FT6X36_STATE_ERROR;

    return FT6X36_ERROR;
}

/**
  * @brief  执行 FT6X36 硬件复位并确认目标 I2C 地址可应答。
  * @param  hft6x36 已由 Platform 绑定 PortOps、Context 和 7-bit 地址的 Handle。
  * @retval FT6X36_OK 控制器已完成复位且 I2C 可访问。
  * @retval FT6X36_ERROR 参数、绑定、复位后的 I2C 探测失败。
  * @note   复位低电平保持和释放后的稳定时间属于 FT6X36 器件协议，而不是
  *         当前 PCB 的策略。调用成功后才允许读取控制器寄存器。
  */
FT6X36_StatusTypeDef FT6X36_Init(FT6X36_HandleTypeDef *hft6x36)
{
    FT6X36_PortStatusTypeDef port_status;

    if (hft6x36 == NULL)
    {
        return FT6X36_ERROR;
    }

    if ((hft6x36->PortOps == NULL) ||
        (hft6x36->PortContext == NULL) ||
        (hft6x36->PortOps->SetReset == NULL) ||
        (hft6x36->PortOps->DelayMs == NULL) ||
        (hft6x36->PortOps->IsReady == NULL))
    {
        hft6x36->ErrorCode = FT6X36_ERROR_PORT_NOT_BOUND;
        hft6x36->State = FT6X36_STATE_ERROR;
        return FT6X36_ERROR;
    }

    if (hft6x36->Address7Bit > 0x7Fu)
    {
        hft6x36->ErrorCode = FT6X36_ERROR_INVALID_PARAM;
        hft6x36->State = FT6X36_STATE_ERROR;
        return FT6X36_ERROR;
    }

    hft6x36->State = FT6X36_STATE_RESET;
    hft6x36->ErrorCode = FT6X36_ERROR_NONE;
    hft6x36->LastPortStatus = FT6X36_PORT_OK;
    hft6x36->LastFailedRegister = 0u;

    hft6x36->PortOps->SetReset(hft6x36->PortContext, true);
    hft6x36->PortOps->DelayMs(hft6x36->PortContext,
                              FT6X36_RESET_ASSERT_DELAY_MS);
    hft6x36->PortOps->SetReset(hft6x36->PortContext, false);
    hft6x36->PortOps->DelayMs(hft6x36->PortContext,
                              FT6X36_RESET_RELEASE_DELAY_MS);

    port_status = hft6x36->PortOps->IsReady(hft6x36->PortContext,
                                            hft6x36->Address7Bit);
    if (port_status != FT6X36_PORT_OK)
    {
        return ft6x36_record_port_failure(hft6x36,
                                           FT6X36_ERROR_PROBE,
                                           port_status,
                                           0u);
    }

    hft6x36->State = FT6X36_STATE_READY;
    return FT6X36_OK;
}

/**
  * @brief  读取 FT6X36 的芯片标识寄存器。
  * @param  hft6x36 已成功初始化的 FT6X36 Handle。
  * @param  chip_id 接收控制器返回原始标识字节的有效地址。
  * @retval FT6X36_OK 标识字节读取成功。
  * @retval FT6X36_ERROR 参数无效、Device 未就绪或 I2C 读取失败。
  * @note   当前不预设或比较固定 ID 值：已验证的旧工程将同一地址的控制器称为
  *         FT6336U，而当前资料命名为 FT6X36。先记录板上实际值，再决定是否为
  *         当前 PCB 增加型号白名单。
  */
FT6X36_StatusTypeDef FT6X36_ReadID(FT6X36_HandleTypeDef *hft6x36,
                                    uint8_t *chip_id)
{
    FT6X36_PortStatusTypeDef port_status;

    if ((hft6x36 == NULL) || (chip_id == NULL))
    {
        return FT6X36_ERROR;
    }

    if ((hft6x36->PortOps == NULL) ||
        (hft6x36->PortContext == NULL) ||
        (hft6x36->PortOps->MemRead == NULL))
    {
        hft6x36->ErrorCode = FT6X36_ERROR_PORT_NOT_BOUND;
        hft6x36->State = FT6X36_STATE_ERROR;
        return FT6X36_ERROR;
    }

    if (hft6x36->State != FT6X36_STATE_READY)
    {
        hft6x36->ErrorCode = FT6X36_ERROR_NOT_READY;
        return FT6X36_ERROR;
    }

    hft6x36->State = FT6X36_STATE_BUSY;
    port_status = hft6x36->PortOps->MemRead(hft6x36->PortContext,
                                            hft6x36->Address7Bit,
                                            FT6X36_REGISTER_CHIP_ID,
                                            chip_id,
                                            1u);
    if (port_status != FT6X36_PORT_OK)
    {
        return ft6x36_record_port_failure(hft6x36,
                                           FT6X36_ERROR_READ_CHIP_ID,
                                           port_status,
                                           FT6X36_REGISTER_CHIP_ID);
    }

    hft6x36->LastPortStatus = FT6X36_PORT_OK;
    hft6x36->LastFailedRegister = 0u;
    hft6x36->ErrorCode = FT6X36_ERROR_NONE;
    hft6x36->State = FT6X36_STATE_READY;
    return FT6X36_OK;
}
