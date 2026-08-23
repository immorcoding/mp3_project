# GUI Service

本 Module 持有 LVGL v8.3.11 的运行时实例，把 SquareLine 生成的 `GUI/` 静态界面、Platform LCD 的异步矩形写入和 Platform Touch 的轮询式原始触点组合为 GUI Task 可调用的产品级流程。

它不实现 ST7789 命令、SPI DMA、FT6X36 寄存器协议、FMC/SDRAM 初始化，也不创建 FreeRTOS Task；这些职责分别属于 Platform、下层 Component/Adapter 和 APP。

## 公开 Interface

- `Service_GUI_Init()`：仅由 GUI Task 调用一次。初始化 LVGL，注册 v8 显示驱动与 Pointer 输入驱动，绑定 LCD DMA 最终回调，并调用 SquareLine 的 `ui_init()`。
- `Service_GUI_Process()`：仅在同一 GUI Task 上下文周期调用。按 FreeRTOS Tick 推进 LVGL 时间，并调用 `lv_timer_handler()` 处理刷新、动画和输入。

重复调用 `Service_GUI_Init()` 返回 `SERVICE_BUSY`。初始化失败后由 APP 决定停机或恢复策略。

## 编译期依赖

- LVGL v8.3.11 的公开入口 `lvgl.h`；
- SquareLine 生成的 `GUI/ui.h`；
- `Platform/lcd`、`Platform/touch` 的公开 Interface；
- FreeRTOS 的任务通知和 Tick Interface。

本 Module 不包含 STM32 HAL、CubeMX 外设 Handle 或 LCD/触摸的 Adapter 头文件。

## 资源与运行时路径

两块完整 RGB565 绘制缓冲由本 Module 静态持有，并通过 `.sdram_framebuffer` 放入外部 SDRAM。缓冲首地址按 `PLATFORM_DMA_BUFFER_ALIGNMENT` 对齐；SPI DMA 发起前的 D-Cache Clean 仍由下层 LCD Adapter 负责。

```text
GUI Task
  -> Service_GUI_Process()
  -> lv_timer_handler()
  -> LVGL v8 flush_cb
  -> Platform_LCD_StartWrite()
  -> ST7789 / SPI DMA
  -> Platform LCD 最终 ISR 回调
  -> Service GUI: lv_disp_flush_ready() + GUI Task 通知
  -> LVGL wait_cb 继续刷新
```

触摸路径为：

```text
LVGL Pointer read_cb
  -> Platform_Touch_ReadRawPoint()
  -> FT6X36 Device / I2C Adapter
```

触摸方向、镜像或校准属于 GUI 语义，只能在本 Module 的 `read_cb` 中处理，不能下沉到 Platform Touch。

## 并发约束

- 除 LCD DMA 最终 ISR 中的 `lv_disp_flush_ready()` 外，所有 LVGL API 只能由 GUI Task 调用；
- ISR 只归还当前 flush 缓冲并发送 FromISR 通知，不读取触摸、不创建对象、不执行布局或日志；
- Platform LCD 同时只允许一笔异步传输，GUI Service 通过 LVGL `wait_cb` 等待该传输的最终事件；
- GUI Task 不得在 LCD DMA 正在读取的绘制缓冲上进行 CPU 写入。

## 私有配置

`gui_service_config.h` 保存绘制缓冲行数。改变该值会同时影响 SDRAM 占用、SPI 刷新分块数量和 LVGL 的双缓冲等待行为，必须结合显示帧率与 D-Cache 约束验证。
