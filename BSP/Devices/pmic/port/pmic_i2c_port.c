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
#include "Devices/pmic/port/pmic_i2c_port.h"

/* PMIC I2C PORT BACKEND BEGIN: Includes -------------------------------------*/
/* 当前后端：GPIO 模拟的软件 I2C。替换硬件 I2C 时修改本区域。 */
#include "Bus/soft_i2c/soft_i2c.h"
#include "main.h"
/* PMIC I2C PORT BACKEND END: Includes ---------------------------------------*/

/* PMIC I2C PORT BACKEND BEGIN: Configuration -------------------------------*/
#define PMIC_I2C_PORT_DELAY_CYCLES           800u   //软件 I2C 每个时序阶段的忙等待循环次数；不是微秒值。
#define PMIC_I2C_PORT_CLOCK_STRETCH_TIMEOUT  1000u  //释放 SCL 后等待其变高的最大轮询次数。

/**
  * @brief 当前 PMIC I2C 后端的私有上下文。
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
/* PMIC I2C PORT BACKEND END: Configuration ---------------------------------*/

/* Private functions ---------------------------------------------------------*/
/* PMIC I2C PORT BACKEND BEGIN: Implementation ------------------------------*/
/**
  * @brief  将 I2C 状态转换为 PMIC 总线统一状态。
  * @param  hi2c   I2C 句柄，用于取得底层原始 ErrorCode。
  * @param  status I2C 函数返回值。
  * @retval PMIC_BusResultTypeDef 归一化状态以及未经修改的底层错误码。
  */
static PMIC_BusResultTypeDef pmic_i2c_port_result(
    const void *hi2c,
    uint32_t status)
{
/*******************若更改硬件 I2C 需要修改此部分，I2C 状态返回值转换****************/
    PMIC_BusResultTypeDef result =
    {
        .Status = (status == SOFT_I2C_OK) ? PMIC_BUS_OK : PMIC_BUS_ERROR,
        .Detail = (hi2c != NULL) ? ((const SoftI2C_HandleTypeDef *)hi2c)->ErrorCode : SOFT_I2C_ERROR_INVALID_PARAM
    };

    if (status == SOFT_I2C_BUSY)
    {
        result.Status = PMIC_BUS_BUSY;
    }
    else if (status == SOFT_I2C_TIMEOUT)
    {
        result.Status = PMIC_BUS_TIMEOUT;
    }
    else if ((status == SOFT_I2C_ERROR) &&
             (result.Detail & (SOFT_I2C_ERROR_NACK_ADDRESS | SOFT_I2C_ERROR_NACK_DATA)))
    {
        result.Status = PMIC_BUS_NACK;
    }
/*********************************************************************************/

    return result;
}

/**
  * @brief  初始化当前软件 I2C 后端。
  * @param  context 必须指向 SoftI2C_HandleTypeDef。
  * @retval PMIC_BusResultTypeDef 总线准备结果。
  */
static PMIC_BusResultTypeDef pmic_i2c_port_prepare(void *context)
{
/*******************若更改硬件 I2C 需要修改此部分，I2C 初始化的用户函数****************/
    SoftI2C_HandleTypeDef *hi2c = (SoftI2C_HandleTypeDef *)context;
    SoftI2C_StatusTypeDef status = SoftI2C_Init(hi2c);
/*********************************************************************************/

    return pmic_i2c_port_result(hi2c, (uint32_t)status);
}

/**
  * @brief  通过当前软件 I2C 后端读取 PMIC 的 8 位寄存器。
  * @param  context             必须指向 SoftI2C_HandleTypeDef。
  * @param  device_address_7bit 7 位从机地址，不包含读写位。
  * @param  reg                 起始寄存器地址。
  * @param  data                接收缓冲区。
  * @param  size                待读取字节数。
  * @retval PMIC_BusResultTypeDef 寄存器读取结果。
  */
static PMIC_BusResultTypeDef pmic_i2c_port_mem_read(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    uint8_t *data,
    uint16_t size)
{
/*******************若更改硬件 I2C 需要修改此部分，I2C MemRead 用户函数****************/
    SoftI2C_HandleTypeDef *hi2c = (SoftI2C_HandleTypeDef *)context;
    SoftI2C_StatusTypeDef status =
        SoftI2C_MemRead(hi2c,
                        device_address_7bit,
                        reg,
                        SOFT_I2C_MEM_ADDR_8BIT,
                        data,
                        size);
/*********************************************************************************/

    return pmic_i2c_port_result(hi2c, (uint32_t)status);
}

/**
  * @brief  通过当前软件 I2C 后端写入 PMIC 的 8 位寄存器。
  * @param  context             软件 I2C 必须指向 SoftI2C_HandleTypeDef。
  * @param  device_address_7bit 7 位从机地址，不包含读写位。
  * @param  reg                 起始寄存器地址。
  * @param  data                发送缓冲区。
  * @param  size                待写入字节数。
  * @retval PMIC_BusResultTypeDef 寄存器写入结果。
  */
static PMIC_BusResultTypeDef pmic_i2c_port_mem_write(
    void *context,
    uint8_t device_address_7bit,
    uint8_t reg,
    const uint8_t *data,
    uint16_t size)
{

/*******************若更改硬件 I2C 需要修改此部分，I2C MemWrite 用户函数****************/
    SoftI2C_HandleTypeDef *hi2c = (SoftI2C_HandleTypeDef *)context;
    SoftI2C_StatusTypeDef status =
        SoftI2C_MemWrite(hi2c,
                         device_address_7bit,
                         reg,
                         SOFT_I2C_MEM_ADDR_8BIT,
                         data,
                         size);
/*********************************************************************************/

    return pmic_i2c_port_result(hi2c, (uint32_t)status);
}
/* PMIC I2C PORT BACKEND END: Implementation --------------------------------*/

/* Private variables ---------------------------------------------------------*/
/** @brief 当前 I2C 后端对 PMIC_BusOpsTypeDef 的私有实现表。 */
static const PMIC_BusOpsTypeDef pmic_i2c_port_ops =
{
    .Prepare = pmic_i2c_port_prepare,
    .MemRead = pmic_i2c_port_mem_read,
    .MemWrite = pmic_i2c_port_mem_write
};

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  为 PMIC 句柄安装当前 I2C 后端的操作表和上下文。
  * @param  hpmic 待绑定的 PMIC 句柄。
  * @retval PMIC_OK    绑定成功。
  * @retval PMIC_ERROR hpmic 为空。
  */
PMIC_StatusTypeDef PMIC_I2C_Port_Bind(PMIC_HandleTypeDef *hpmic)
{
    if (hpmic == NULL)
    {
        return PMIC_ERROR;
    }

    hpmic->BusOps = &pmic_i2c_port_ops;
    hpmic->BusContext = &hpmic_i2c;

    return PMIC_OK;
}
