/**
  ******************************************************************************
  * @file    pmic_i2c_port.h
  * @brief   PMIC I2C 端口的板级绑定接口。
  *
  * @details
  *          本模块向 Board 层隐藏当前使用的软件 I2C 或硬件 I2C 实现。
  *          Board 层只需调用 PMIC_I2C_Port_Bind()，无需接触底层总线句柄类型。
  ******************************************************************************
  */

#ifndef PMIC_I2C_PORT_H
#define PMIC_I2C_PORT_H

/* Includes ------------------------------------------------------------------*/
#include "Devices/pmic/pmic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  为 PMIC 句柄绑定本板当前采用的 I2C 后端。
  * @param  hpmic 待绑定的 PMIC 句柄。
  * @note   本函数只安装 BusOps 和 BusContext，不访问总线，也不初始化 PMIC。
  * @retval PMIC_OK    绑定成功。
  * @retval PMIC_ERROR hpmic 为空。
  */
PMIC_StatusTypeDef PMIC_I2C_Port_Bind(PMIC_HandleTypeDef *hpmic);

#ifdef __cplusplus
}
#endif

#endif /* PMIC_I2C_PORT_H */
