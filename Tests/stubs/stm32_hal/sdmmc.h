/**
 * @file sdmmc.h
 * @brief 主机测试用的 CubeMX `sdmmc.h` 替身：只声明 Platform SD 借用的 HAL 句柄。
 */
#ifndef HOST_STUB_SDMMC_H
#define HOST_STUB_SDMMC_H

#include "stm32h7xx_hal.h"

/** @brief CubeMX 生成的 SDMMC1 句柄；测试文件里定义这个对象。 */
extern SD_HandleTypeDef hsd1;

#endif /* HOST_STUB_SDMMC_H */
