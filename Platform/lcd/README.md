# Platform LCD

本 Module 装配当前 PCB 唯一的 ST7789 Device、SPI1、CS/D-C/RESET GPIO 和 AXP2101 的 LCD 电源语义。当前只提供复位和 RDDID 诊断，不代表已经完成显示初始化或 LVGL 显示接口。

## 公开 Interface

- `Platform_LCD_Init()`：开启 LCD 电源并完成 SPI/GPIO Adapter 绑定与 ST7789 硬件复位；
- `Platform_LCD_ReadID()`：读取 RDDID 并返回与 Component 类型解耦的 `Platform_LCD_IDTypeDef`。

## 编译期依赖与装配

- `Platform_Power_SetLCD()`；
- `ST7789_SPI_STM32HALAdapter_Bind()`；
- `ST7789_Init()` 与 `ST7789_ReadID()`；
- 初始化失败时使用 `LOG_Printf()` 记录诊断。

## 运行时请求与事件路径

上层经 `Platform_LCD_*` 请求当前板的显示能力；本 Module 调用 ST7789 Device，Device 再通过已绑定 SPI/GPIO Adapter 访问硬件。当前为阻塞诊断路径，没有 LCD ISR、DMA 完成事件或 LVGL flush 回调。

## 演进约束

当前只有一个真实 LCD Controller，`Platform_LCD_*` 已是上层稳定 Seam，不建立空的 `display` Component 或 bridge。出现第二个真实 Controller，或 LVGL 已形成稳定的通用消费契约时，再提取 `Components/display` 与 `Adapters/bridge/<controller>_display`。

## 禁止依赖

不允许 APP 直接读取 `hspi1` 或 LCD GPIO，也不在本 Module 中创建 FreeRTOS Task、调用 LVGL、申请帧缓冲或启动 DMA。

## 命名

跨层公开 Interface 使用 `Platform_LCD_*`；文件内私有实例使用 `hplatform_lcd*`。
