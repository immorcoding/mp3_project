/**
  ******************************************************************************
  * @file    pmic_i2c_port.h
  * @brief   PMIC I2C 端口的板级绑定接口。
  *
  * @details
  *          本模块向 Board 层隐藏当前使用的软件 I2C 或硬件 I2C 实现。
  *          Board 层只需调用 PMIC_I2C_Port_Bind()，无需接触底层总线句柄类型。
  *          Port 是“翻译层”：一侧固定为 PMIC_BusOpsTypeDef，另一侧可以是
  *          SoftI2C、HAL I2C 或测试桩。
  ******************************************************************************
  */

#ifndef PMIC_I2C_PORT_H
#define PMIC_I2C_PORT_H

/* Includes ------------------------------------------------------------------*/
#include "BSP/Devices/pmic/pmic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  为 PMIC 句柄绑定本板当前采用的 I2C 后端。
  * @param  hpmic 待绑定的 PMIC 句柄。
  * @note   本函数只安装 BusOps 和 BusContext，不访问总线，也不初始化 PMIC。
  *         Ops 与 Context 必须成对使用，绑定后应交由 PMIC_Init() 校验。
  * @retval PMIC_OK    绑定成功。
  * @retval PMIC_ERROR hpmic 为空。
  */
PMIC_StatusTypeDef PMIC_I2C_Port_Bind(PMIC_HandleTypeDef *hpmic);

#ifdef __cplusplus
}
#endif

#endif /* PMIC_I2C_PORT_H */
