/**
  ******************************************************************************
  * @file    ft6x36_config.h
  * @brief   FT6X36 Device 的私有协议参数。
  ******************************************************************************
  */

#ifndef FT6X36_CONFIG_H
#define FT6X36_CONFIG_H

/* FT6X36 系列的芯片标识寄存器。 */
#define FT6X36_REGISTER_CHIP_ID                 0xA3u

/* TD_STATUS 的低 4 位表示当前可读取的触点数量。 */
#define FT6X36_REGISTER_TD_STATUS               0x02u
#define FT6X36_TD_STATUS_TOUCH_COUNT_MASK       0x0Fu
/* 第一触点的 XH、XL、YH、YL 连续位于 TD_STATUS 之后。 */
#define FT6X36_TOUCH_POINT_FRAME_SIZE            5u
#define FT6X36_TOUCH_COORDINATE_HIGH_MASK        0x0Fu

#endif /* FT6X36_CONFIG_H */
