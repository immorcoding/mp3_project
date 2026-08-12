/**
  ******************************************************************************
  * @file    axp2101_soft_i2c_adapter.h
  * @brief   SoftI2C 到 AXP2101 Bus Interface 的 Adapter。
  ******************************************************************************
  */

#ifndef AXP2101_SOFT_I2C_ADAPTER_H
#define AXP2101_SOFT_I2C_ADAPTER_H

#include "Components/soft_i2c/soft_i2c.h"
#include "Components/axp2101/axp2101.h"

#ifdef __cplusplus
extern "C" {
#endif

AXP2101_StatusTypeDef AXP2101_SoftI2CAdapter_Bind(
    AXP2101_HandleTypeDef *haxp2101,
    SoftI2C_HandleTypeDef *hi2c);

#ifdef __cplusplus
}
#endif

#endif /* AXP2101_SOFT_I2C_ADAPTER_H */
