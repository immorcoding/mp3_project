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
  *          Bind 只成对安装 Ops 和 Context，不初始化总线，也不产生 I2C 波形。
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
PMIC_StatusTypeDef PMIC_I2C_Port_Bind(PMIC_HandleTypeDef *hpmic);

#ifdef __cplusplus
}
#endif

#endif /* PMIC_I2C_PORT_H */
