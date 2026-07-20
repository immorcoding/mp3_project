/**
  ******************************************************************************
  * @file    board_pmic.h
  * @brief   本板 PMIC 设备实例和初始化接口。
  ******************************************************************************
  */

#ifndef BOARD_PMIC_H
#define BOARD_PMIC_H

/* Includes ------------------------------------------------------------------*/
#include "Devices/pmic/pmic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  绑定本板 PMIC I2C 端口并初始化 AXP2101。
  * @retval PMIC_OK    端口绑定、芯片识别和默认配置全部成功。
  * @retval PMIC_ERROR 端口绑定或 PMIC 初始化失败。
  */
PMIC_StatusTypeDef Board_PMIC_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_PMIC_H */
