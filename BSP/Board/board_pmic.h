/**
  ******************************************************************************
  * @file    board_pmic.h
  * @brief   本板 PMIC 设备实例和初始化接口。
  *
  * @details
  *          Board 层把“本 PCB 上有一颗怎样连接的 PMIC”封装成单一入口。
  *          Application 层不需要知道 AXP2101 地址、软件/硬件 I2C 类型或
  *          GPIO 引脚。若未来更换总线后端，Board 的公共接口保持不变。
  ******************************************************************************
  */

#ifndef BOARD_PMIC_H
#define BOARD_PMIC_H

/* Includes ------------------------------------------------------------------*/
#include "BSP/Devices/pmic/pmic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  绑定本板 PMIC I2C 端口并初始化 AXP2101。
  * @note   本函数可以在非并发条件下重复调用；每次都会重新绑定后端、探测
  *         芯片并按 PMIC 默认策略重新写入启动配置。
  * @retval PMIC_OK    端口绑定、芯片识别和默认配置全部成功。
  * @retval PMIC_ERROR 端口绑定或 PMIC 初始化失败。
  */
PMIC_StatusTypeDef Board_PMIC_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_PMIC_H */
