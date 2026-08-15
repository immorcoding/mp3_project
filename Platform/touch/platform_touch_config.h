/**
  ******************************************************************************
  * @file    platform_touch_config.h
  * @brief   当前 PCB 的 FT6X36 触摸装配参数。
  ******************************************************************************
  */

#ifndef PLATFORM_TOUCH_CONFIG_H
#define PLATFORM_TOUCH_CONFIG_H

/* 当前触摸模组的 7-bit I2C 从机地址。HAL 左移一位的表示仅由 Adapter 处理。 */
#define PLATFORM_TOUCH_I2C_ADDRESS_7BIT          0x38u
/* 启动阶段允许的 I2C 应答尝试次数。 */
#define PLATFORM_TOUCH_I2C_PROBE_TRIALS           5u
/* 单次触摸 I2C 探测或寄存器访问的同步超时，单位为毫秒。 */
#define PLATFORM_TOUCH_I2C_TIMEOUT_MS             100u

#endif /* PLATFORM_TOUCH_CONFIG_H */
