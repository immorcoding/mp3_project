# Platform LCD

本 Module 装配当前 PCB 唯一的 ST7789 Device、SPI1、CS/D-C/RESET/背光 GPIO 和 AXP2101 的 LCD 电源语义。当前提供 RGB565 最小亮屏、RDDID、阻塞画点/填充，以及异步 DMA 矩形写入的稳定产品 Interface；不拥有帧缓冲或 LVGL。

## 公开 Interface

- `Platform_LCD_Init()`：开启 LCD 电源，完成 SPI/GPIO Adapter 绑定、ST7789 RGB565 显示初始化并打开背光；当前由 `Platform_Init()` 在调度器启动前调用一次；
- `Platform_LCD_ReadID()`：读取 RDDID 并返回与 Component 类型解耦的 `Platform_LCD_IDTypeDef`。
- `Platform_LCD_DrawPixel()`、`Platform_LCD_FillRect()`、`Platform_LCD_FillScreen()`：对上提供 RGB565 绘制能力，不泄漏 ST7789 命令与 SPI 细节。
- `Platform_LCD_SetTransferCallback()`：由上层任务注册唯一的 ISR 轻量回调；Platform 不包含 FreeRTOS，因此不直接发送任务通知；
- `Platform_LCD_StartWrite()`：启动一个完整 RGB565 矩形的 SPI DMA 写入。返回 `PLATFORM_OK` 仅表示首 DMA 块已开始；最终结果由已注册回调发布，`PLATFORM_BUSY` 表示上一笔仍在飞。

## 编译期依赖与装配

- `Platform_Power_SetLCD()`；
- `ST7789_SPI_STM32HALAdapter_Bind()`；
- `ST7789_Init()` 与 `ST7789_ReadID()`；
- 初始化失败时使用 `LOG_Printf()` 记录诊断。

## 运行时请求与事件路径

上层经 `Platform_LCD_*` 请求当前板的显示能力；本 Module 调用 ST7789 Device，Device 再通过已绑定 SPI/GPIO Adapter 访问硬件。DMA 最终事件从 HAL SPI 回调经 Adapter、ST7789 Device 到达本 Module，再转发给上层已注册的回调。Platform 只传递强类型完成/错误事件，不认识任务句柄、LVGL 或双缓冲。

## 演进约束

当前只有一个真实 LCD Controller，`Platform_LCD_*` 已是上层稳定 Seam，不建立空的 `display` Component 或 bridge。出现第二个真实 Controller，或 LVGL 已形成稳定的通用消费契约时，再提取 `Components/display` 与 `Adapters/bridge/<controller>_display`。

## 私有配置

`platform_lcd_config.h` 集中保存当前 PCB 的 LCD 电源稳定时间、同步 SPI 超时和背光有效极性。它只由 `platform_lcd.c` 包含；上层不能依据该文件操作 GPIO 或推断 ST7789 协议。

## 禁止依赖

不允许 APP 直接读取 `hspi1` 或 LCD GPIO，也不在本 Module 中创建 FreeRTOS Task、调用 LVGL、申请帧缓冲，或在 ISR 回调内执行业务。DMA 的 HAL 启动与分块属于 Adapter；双绘制缓冲属于 `Service/gui`。Platform 本身不调用 LVGL，只有 GUI Service 注册的最终 ISR 回调可调用 `lv_disp_flush_ready()` 归还已完成的 flush 缓冲，随后只发送 FromISR 通知。

## 命名

跨层公开 Interface 使用 `Platform_LCD_*`；文件内私有实例使用 `hplatform_lcd*`。
