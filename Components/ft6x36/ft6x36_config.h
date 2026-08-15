/**
  ******************************************************************************
  * @file    ft6x36_config.h
  * @brief   FT6X36 Device 的私有协议与复位参数。
  ******************************************************************************
  */

#ifndef FT6X36_CONFIG_H
#define FT6X36_CONFIG_H

/* FT6X36 系列的芯片标识寄存器。 */
#define FT6X36_REGISTER_CHIP_ID                 0xA3u

/* 已在本项目旧版硬件上验证的复位时序，单位为毫秒。 */
#define FT6X36_RESET_ASSERT_DELAY_MS            5u
#define FT6X36_RESET_RELEASE_DELAY_MS            200u

#endif /* FT6X36_CONFIG_H */
