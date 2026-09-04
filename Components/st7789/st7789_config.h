/**
  ******************************************************************************
  * @file    st7789_config.h
  * @brief   ST7789 Device 的私有固定配置。
  *
  * @details
  *          本文件仅供 `Components/st7789` 的 Implementation 使用，集中保存
  *          当前 ST7789V 最小 RGB565 初始化、复位时序和阻塞填充块参数。
  *          它不是跨 Module 的公开 Interface；上层只能使用 `st7789.h`。
  ******************************************************************************
  */

#ifndef ST7789_CONFIG_H
#define ST7789_CONFIG_H

/* st7789.h */
#define ST7789_WIDTH   240u /* 当前模组可见区域水平像素数。 */
#define ST7789_HEIGHT  320u /* 当前模组可见区域垂直像素数。 */

/* st7789.c */
#define ST7789_COMMAND_RDDID                 0x04u /* ST7789 的 Read Display Identification 命令。 */
#define ST7789_COMMAND_SLPOUT                0x11u /* 退出休眠并启动内部显示时钟。 */
#define ST7789_COMMAND_NORON                 0x13u /* 进入正常显示模式。 */
#define ST7789_COMMAND_INVON                 0x21u /* 开启显示反相驱动，适合当前 IPS 模组的最小显示初始化。 */
#define ST7789_COMMAND_DISPON                0x29u /* 打开显示输出。 */
#define ST7789_COMMAND_CASET                 0x2Au /* 设置列地址窗口。 */
#define ST7789_COMMAND_RASET                 0x2Bu /* 设置行地址窗口。 */
#define ST7789_COMMAND_RAMWR                 0x2Cu /* 开始向 Display RAM 连续写入像素。 */
#define ST7789_COMMAND_MADCTL                0x36u /* 设置显示扫描方向和 RGB/BGR 顺序。 */
#define ST7789_COMMAND_COLMOD                0x3Au /* 设置 MCU 接口写入像素格式。 */
#define ST7789_COMMAND_FRCTRL2               0xC6u /* 设置正常显示模式下的面板扫描帧率。 */

#define ST7789_SLEEP_OUT_DELAY_MS             120u /* 退出休眠后等待内部模拟电路稳定的时间，单位为毫秒。 */
#define ST7789_NORMAL_MODE_DELAY_MS            10u /* 进入正常显示模式后等待扫描稳定的时间，单位为毫秒。 */
#define ST7789_DISPLAY_ON_DELAY_MS             20u /* 打开显示输出后等待首帧建立的时间，单位为毫秒。 */
#define ST7789_RESET_ASSERT_DELAY_MS           10u /* 硬件复位低电平保持时间，单位为毫秒。 */
#define ST7789_RESET_RELEASE_DELAY_MS         120u /* 释放 RESET 后等待 NVM 设置装载的时间，单位为毫秒。 */

#define ST7789_COLMOD_RGB565                  0x55u /* 16 位 RGB565 像素格式对应的 COLMOD 参数。 */
#define ST7789_MADCTL_RGB_TOP_LEFT            0x00u /* 左上为原点、从左到右和从上到下、RGB 像素顺序的 MADCTL 参数。 */
#define ST7789_FRCTRL2_40HZ                   0x1Eu /* 正常显示模式约 FRCTRL2 参数。 该数值按数据手册默认前后 porch 参数对应约 40 Hz / 60 Hz。 */
#define ST7789_FRCTRL2_60HZ                   0x0Fu
#define ST7789_FILL_BUFFER_PIXELS              128u /* 每次 SPI 阻塞写入的纯色像素数量。 */
#define ST7789_RDDID_TRANSFER_LENGTH             4u /* RDDID 的 24 位返回数据需要额外产生的完整串行字节数。 */

#endif /* ST7789_CONFIG_H */
