/**
  ******************************************************************************
  * @file    ft6x36_config.h
  * @brief   FT6X36 Device 的私有协议参数。
  ******************************************************************************
  */

#ifndef FT6X36_CONFIG_H
#define FT6X36_CONFIG_H

/* ft6x36.c */
#define FT6X36_REGISTER_CHIP_ID            0xA3u  /* FT6X36 系列的芯片标识寄存器。 */
#define FT6X36_REGISTER_TD_STATUS          0x02u  /* 触点状态寄存器；低 4 位为当前可读取触点数。 */
#define FT6X36_TD_STATUS_TOUCH_COUNT_MASK  0x0Fu  /* TD_STATUS 中触点数量字段掩码。 */

#define FT6X36_TOUCH_POINT_FRAME_SIZE      5u     /* 第一触点 XH/XL/YH/YL 连续帧的字节数。 */
#define FT6X36_TOUCH_COORDINATE_HIGH_MASK  0x0Fu  /* 坐标高字节中有效位掩码。 */

#endif /* FT6X36_CONFIG_H */
