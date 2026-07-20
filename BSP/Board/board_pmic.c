/**
  ******************************************************************************
  * @file    board_pmic.c
  * @brief   本板 PMIC 设备实例的装配实现。
  *
  * @details
  *          Board 层只保存 PMIC 实例并组织初始化调用，不直接装载句柄字段；
  *          当前使用软件 I2C 还是硬件 I2C，由 pmic_i2c_port.c 内部决定。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "BSP/Board/board_pmic.h"

#include "BSP/Devices/pmic/port/pmic_i2c_port.h"

/* Private variables ---------------------------------------------------------*/
/**
  * @brief 本板 PMIC 设备实例。
  * @note  总线依赖由端口绑定函数注入，默认设备配置及运行状态由
  *        PMIC_Init() 装载，Board 层不直接修改句柄字段。
  */
static PMIC_HandleTypeDef hpmic;

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  绑定本板 PMIC I2C 后端并初始化 AXP2101。
  * @retval PMIC_OK    初始化成功。
  * @retval PMIC_ERROR 端口绑定或 PMIC 初始化失败。
  */
PMIC_StatusTypeDef Board_PMIC_Init(void)
{
    if (PMIC_I2C_Port_Bind(&hpmic) != PMIC_OK)
    {
        return PMIC_ERROR;
    }

    return PMIC_Init(&hpmic);
}
