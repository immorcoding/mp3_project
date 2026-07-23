/**
  ******************************************************************************
  * @file    pmic_i2c_port.c
  * @brief   PMIC I2C 端口实现。
  *
  * @details
  *          文件结构参考常见的 LVGL Port 写法：带 BACKEND BEGIN/END 标记的
  *          区域属于当前总线后端，标记外是稳定的 PMIC 绑定接口。以后切换 HAL
  *          硬件 I2C 时，只替换这些标记区域中的包含文件、上下文和适配函数即可。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "pmic_i2c_port.h"

/* PMIC I2C PORT BACKEND BEGIN: Includes -------------------------------------*/
/* 当前后端：GPIO 模拟的软件 I2C。替换硬件 I2C 时修改本区域。 */
#include "BSP/Bus/soft_i2c/soft_i2c.h"
#include "main.h"
/* PMIC I2C PORT BACKEND END: Includes ---------------------------------------*/

/* PMIC I2C PORT BACKEND BEGIN: Configuration -------------------------------*/
/** @brief 软件 I2C 每个时序阶段的忙等待循环次数；不是微秒值。 */
#define PMIC_I2C_PORT_DELAY_CYCLES           800u
/** @brief 释放 SCL 后等待其实际变高的最大轮询次数。 */
#define PMIC_I2C_PORT_CLOCK_STRETCH_TIMEOUT  1000u

/*******i2c实例，用户自定义，也可使用hal库的示例，就不需要定义这个SoftI2C_HandleTypeDef********/
/**
  * @brief 当前 PMIC I2C 后端的私有上下文。软件 I2C。
  * @note  名称 hpmic_i2c 应在切换后端时保持不变，类型和初始化字段可以替换。
  */
static SoftI2C_HandleTypeDef hpmic_i2c =
{
    .SCL_Port = AXP2101_SCL_GPIO_Port,
    .SCL_Pin = AXP2101_SCL_Pin,
    .SDA_Port = AXP2101_SDA_GPIO_Port,
    .SDA_Pin = AXP2101_SDA_Pin,
    .DelayCycles = PMIC_I2C_PORT_DELAY_CYCLES,
    .ClockStretchTimeout = PMIC_I2C_PORT_CLOCK_STRETCH_TIMEOUT,
    .State = SOFT_I2C_STATE_RESET,
    .ErrorCode = SOFT_I2C_ERROR_NONE
};
/****************************************************************************************/
/* PMIC I2C PORT BACKEND END: Configuration ---------------------------------*/

/* Private functions ---------------------------------------------------------*/
/* PMIC I2C PORT BACKEND BEGIN: Implementation ------------------------------*/
/**
  * @brief  将 I2C 状态转换为 PMIC 总线统一状态。
  * @param  status I2C 函数返回值。
  * @note   SoftI2C 原始 ErrorCode 保留在其私有 Handle 中，不跨越 Port Seam。
  * @retval PMIC_BusStatusTypeDef 归一化后的总线状态。
  */
static PMIC_BusStatusTypeDef pmic_i2c_port_status(
    const SoftI2C_HandleTypeDef *hi2c,
    SoftI2C_StatusTypeDef status)
{
    /* BACKEND: 将 SoftI2C 返回值和 ErrorCode 映射到 PMIC 公共错误模型。 */
    if (status == SOFT_I2C_OK)
    {
        return PMIC_BUS_OK;
    }

    if (status == SOFT_I2C_BUSY)
    {
        return PMIC_BUS_BUSY;
    }

    if (status == SOFT_I2C_TIMEOUT)
    {
        return PMIC_BUS_TIMEOUT;
    }

    if ((status == SOFT_I2C_ERROR) &&
        (hi2c != NULL) &&
        ((hi2c->ErrorCode &
          (SOFT_I2C_ERROR_NACK_ADDRESS | SOFT_I2C_ERROR_NACK_DATA)) != 0u))
    {
        return PMIC_BUS_NACK;
    }
    /* BACKEND END */

    return PMIC_BUS_ERROR;
}

/**
  * @brief  初始化当前软件 I2C 后端。
  * @param  context 必须指向 I2C 句柄。
  * @retval PMIC_BusStatusTypeDef 总线准备结果。
  */
static PMIC_BusStatusTypeDef pmic_i2c_port_prepare(void *context)
{
    /* BACKEND: 恢复具体句柄类型并执行软件 I2C 初始化/自动总线恢复。 */
    SoftI2C_StatusTypeDef status = SoftI2C_Init((SoftI2C_HandleTypeDef *)context);
    /* BACKEND END */

    return pmic_i2c_port_status((const SoftI2C_HandleTypeDef *)context,
                                status);
}

/**
  * @brief  通过当前软件 I2C 后端读取 PMIC 的 8 位寄存器。
  * @param  context             必须指向 I2C 句柄。
  * @param  device_address_7bit 7 位从机地址，不包含读写位。
  * @param  reg                 起始寄存器地址。
  * @param  data                接收缓冲区。
  * @param  size                待读取字节数。
  * @retval PMIC_BusStatusTypeDef 寄存器读取结果。
  */
static PMIC_BusStatusTypeDef pmic_i2c_port_mem_read(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    uint8_t *data,
    uint16_t size)
{
    /* BACKEND: AXP2101 使用 8 位寄存器地址；设备地址保持 7 位形式。 */
    SoftI2C_StatusTypeDef status =
        SoftI2C_MemRead((SoftI2C_HandleTypeDef *)context,
                        device_address_7bit,
                        reg,
                        SOFT_I2C_MEM_ADDR_8BIT,
                        data,
                        size);
    /* BACKEND END */

    return pmic_i2c_port_status((const SoftI2C_HandleTypeDef *)context,
                                status);
}

/**
  * @brief  通过当前软件 I2C 后端写入 PMIC 的 8 位寄存器。
  * @param  context             必须指向 I2C 句柄。
  * @param  device_address_7bit 7 位从机地址，不包含读写位。
  * @param  reg                 起始寄存器地址。
  * @param  data                发送缓冲区。
  * @param  size                待写入字节数。
  * @retval PMIC_BusStatusTypeDef 寄存器写入结果。
  */
static PMIC_BusStatusTypeDef pmic_i2c_port_mem_write(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    const uint8_t *data,
    uint16_t size)
{

    /* BACKEND: 同步写入 8 位寄存器地址和调用者数据。 */
    SoftI2C_StatusTypeDef status = SoftI2C_MemWrite((SoftI2C_HandleTypeDef *)context,
                                                    device_address_7bit,
                                                    reg,
                                                    SOFT_I2C_MEM_ADDR_8BIT,
                                                    data,
                                                    size);
    /* BACKEND END */

    return pmic_i2c_port_status((const SoftI2C_HandleTypeDef *)context,
                                status);
}
/* PMIC I2C PORT BACKEND END: Implementation --------------------------------*/

/* Private variables ---------------------------------------------------------*/
/**
  * @brief 当前 I2C 后端对 PMIC_BusOpsTypeDef 的私有实现表。
  * @note  三个函数签名稳定；切换后端时函数体可变，Device/Board 无需改动。
  */
static const PMIC_BusOpsTypeDef pmic_i2c_port_ops =
{
    .Prepare = pmic_i2c_port_prepare,
    .MemRead = pmic_i2c_port_mem_read,
    .MemWrite = pmic_i2c_port_mem_write
};

/* Exported functions --------------------------------------------------------*/
PMIC_StatusTypeDef PMIC_I2C_Port_Bind(PMIC_HandleTypeDef *hpmic)
{
    if (hpmic == NULL)
    {
        return PMIC_ERROR;
    }

    /* 函数表和它所解释的上下文必须一起安装，构成完整的后端对象。 */
    hpmic->BusOps = &pmic_i2c_port_ops;

/*******i2c实例，用户自定义，也可使用hal库的实例，就不需要定义这个hpmic_i2c********/
    hpmic->BusContext = &hpmic_i2c;
/*************************************************************************** */
    return PMIC_OK;
}
