/**
  ******************************************************************************
  * @file    axp2101_soft_i2c_adapter.c
  * @brief   SoftI2C 到 AXP2101 Bus Interface 的 Adapter 实现。
  *
  * @details
  *          本模块只转换函数调用和状态码，不拥有 GPIO 引脚、总线时序参数
  *          或具体 SoftI2C 实例。具体实例由 Platform Power Module 创建并注入。
  ******************************************************************************
  */

#include "Adapters/axp2101_soft_i2c/axp2101_soft_i2c_adapter.h"
#include <stddef.h>

/**
  * @brief  将 SoftI2C 状态转换为 AXP2101 Device 可理解的总线状态。
  * @param  context SoftI2C Handle，用于读取 NACK 原始错误位。
  * @param  native_status SoftI2C_StatusTypeDef 的整数表示。
  * @retval AXP2101_BusStatusTypeDef 归一化后的总线状态。
  */
static AXP2101_BusStatusTypeDef axp2101_soft_i2c_status(
    const void *context,
    int32_t native_status)
{
    const SoftI2C_HandleTypeDef *hi2c = (const SoftI2C_HandleTypeDef *)context;
    SoftI2C_StatusTypeDef status = (SoftI2C_StatusTypeDef)native_status;

    if (status == SOFT_I2C_OK)
    {
        return AXP2101_BUS_OK;
    }

    if (status == SOFT_I2C_BUSY)
    {
        return AXP2101_BUS_BUSY;
    }

    if (status == SOFT_I2C_TIMEOUT)
    {
        return AXP2101_BUS_TIMEOUT;
    }

    if ((status == SOFT_I2C_ERROR) &&
        (hi2c != NULL) &&
        ((hi2c->ErrorCode &
          (SOFT_I2C_ERROR_NACK_ADDRESS | SOFT_I2C_ERROR_NACK_DATA)) != 0u))
    {
        return AXP2101_BUS_NACK;
    }

    return AXP2101_BUS_ERROR;
}

/**
  * @brief  初始化并检查当前 SoftI2C 实例。
  * @param  context 必须指向 SoftI2C_HandleTypeDef。
  * @retval AXP2101_BusStatusTypeDef 归一化后的总线准备状态。
  */
static AXP2101_BusStatusTypeDef axp2101_soft_i2c_prepare(void *context)
{
    SoftI2C_StatusTypeDef status = SoftI2C_Init((SoftI2C_HandleTypeDef *)context);

    return axp2101_soft_i2c_status(context, (int32_t)status);
}

/**
  * @brief  使用 SoftI2C 读取 AXP2101 的 8 位寄存器。
  * @param  context 必须指向 SoftI2C_HandleTypeDef。
  * @param  device_address_7bit 7 位 I2C 地址。
  * @param  reg 起始寄存器地址。
  * @param  data 接收缓冲区。
  * @param  size 待读取字节数。
  * @retval AXP2101_BusStatusTypeDef 归一化后的读取状态。
  */
static AXP2101_BusStatusTypeDef axp2101_soft_i2c_mem_read(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    uint8_t *data,
    uint16_t size)
{
    SoftI2C_StatusTypeDef status = SoftI2C_MemRead(
        (SoftI2C_HandleTypeDef *)context,
        device_address_7bit,
        reg,
        SOFT_I2C_MEM_ADDR_8BIT,
        data,
        size);

    return axp2101_soft_i2c_status(context, (int32_t)status);
}

/**
  * @brief  使用 SoftI2C 写入 AXP2101 的 8 位寄存器。
  * @param  context 必须指向 SoftI2C_HandleTypeDef。
  * @param  device_address_7bit 7 位 I2C 地址。
  * @param  reg 起始寄存器地址。
  * @param  data 发送缓冲区。
  * @param  size 待写入字节数。
  * @retval AXP2101_BusStatusTypeDef 归一化后的写入状态。
  */
static AXP2101_BusStatusTypeDef axp2101_soft_i2c_mem_write(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    const uint8_t *data,
    uint16_t size)
{
    SoftI2C_StatusTypeDef status = SoftI2C_MemWrite(
        (SoftI2C_HandleTypeDef *)context,
        device_address_7bit,
        reg,
        SOFT_I2C_MEM_ADDR_8BIT,
        data,
        size);

    return axp2101_soft_i2c_status(context, (int32_t)status);
}

static const AXP2101_BusOpsTypeDef axp2101_soft_i2c_ops = {
    .Prepare = axp2101_soft_i2c_prepare,
    .MemRead = axp2101_soft_i2c_mem_read,
    .MemWrite = axp2101_soft_i2c_mem_write
};

/**
  * @brief  把 SoftI2C 实例绑定到 AXP2101 Device Handle。
  * @param  haxp2101 待绑定的 AXP2101 Handle。
  * @param  hi2c 由 Platform 层拥有的 SoftI2C Handle。
  * @retval AXP2101_OK 绑定成功。
  * @retval AXP2101_ERROR 任一参数为空。
  * @note   本函数只安装 Ops 和 Context，不初始化总线，也不产生 I2C 波形。
  */
AXP2101_StatusTypeDef AXP2101_SoftI2CAdapter_Bind(
    AXP2101_HandleTypeDef *haxp2101,
    SoftI2C_HandleTypeDef *hi2c)
{
    if ((haxp2101 == NULL) || (hi2c == NULL))
    {
        return AXP2101_ERROR;
    }

    haxp2101->BusOps = &axp2101_soft_i2c_ops;
    haxp2101->BusContext = hi2c;
    return AXP2101_OK;
}
