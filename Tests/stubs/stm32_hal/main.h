/**
 * @file main.h
 * @brief 主机测试用的 CubeMX `main.h` 替身：只提供 Platform SD 用到的卡检测引脚名。
 */
#ifndef HOST_STUB_MAIN_H
#define HOST_STUB_MAIN_H

#include "stm32h7xx_hal.h"

/** @brief 假的卡检测端口寄存器块；测试文件里定义这个对象。 */
extern GPIO_TypeDef host_stub_sd_cd_port;

#define SD_CD_Pin ((uint16_t)0x0100U)
#define SD_CD_GPIO_Port (&host_stub_sd_cd_port)

#endif /* HOST_STUB_MAIN_H */
