# GUI Service

本 Module 持有 LVGL v8.3.11 的运行时实例，把 SquareLine 生成的 `GUI/` 静态界面、Platform LCD 的异步矩形写入和 Platform Touch 的轮询式原始触点组合为 GUI Task 可调用的产品级流程。

它不实现 ST7789 命令、SPI DMA、FT6X36 寄存器协议、FMC/SDRAM 初始化，也不创建 FreeRTOS Task；这些职责分别属于 Platform、下层 Component/Adapter 和 APP。

## 公开 Interface

- `Service_GUI_Init()`：仅由 GUI Task 调用一次。初始化 LVGL，注册 v8 显示驱动与 Pointer 输入驱动，绑定 LCD DMA 最终回调，并调用 SquareLine 的 `ui_init()`。
- `Service_GUI_Process()`：仅在同一 GUI Task 上下文周期调用。按 FreeRTOS Tick 推进 LVGL 时间，并调用 `lv_timer_handler()` 处理刷新、动画和输入。

重复调用 `Service_GUI_Init()` 返回 `SERVICE_BUSY`。当前 GUI Task 将初始化失败视为致命并调用
`Error_Handler()`；GUI Service 尚未提供部分初始化后的回滚或重试 Interface。

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
  -> Platform_Touch_IsAvailable()
  -> 可用：Platform_Touch_ReadRawPoint()
       -> FT6X36 Device / I2C Adapter
  -> 不可用：向 LVGL 报告 RELEASED，不访问 I2C
```

触摸方向、镜像或校准属于 GUI 语义，只能在本 Module 的 `read_cb` 中处理，不能下沉到 Platform Touch。

## 并发约束

- 除 LCD DMA 最终 ISR 中的 `lv_disp_flush_ready()` 外，所有 LVGL API 只能由 GUI Task 调用；
- ISR 只归还当前 flush 缓冲并发送 FromISR 通知，不读取触摸、不创建对象、不执行布局或日志；
- Platform LCD 同时只允许一笔异步传输，GUI Service 通过 LVGL `wait_cb` 等待该传输的最终事件；
- GUI Task 不得在 LCD DMA 正在读取的绘制缓冲上进行 CPU 写入。

## 私有 Modules 与配置

对外仍只有 `Service_GUI_Init()` 与 `Service_GUI_Process()` 两个 Interface；
以下是 `Service/gui` 内部的实现拆分，不得被 APP 或其他 Service 直接包含或调用。
SquareLine 生成代码唯一允许的例外是由 `GUI/ui_events.h` 声明、GUI Service 实现的
`Service_GUI_Boot_RequestLock()`：它是 BootReveal 的窄事件交接点，不属于供上层调用的
公开 Service Interface，也不要求 `GUI/` 包含任何 Service 头文件。

```text
Service/gui/
├─ gui_service.c / .h / _config.h    GUI Task 生命周期、显示/触摸和 DMA 桥接
├─ boot/                             仅属于启动视觉序列
│  ├─ gui_service_boot.c / .h
│  └─ gui_service_boot_config.h
├─ main/                             Main Screen 的运行时视觉补充
│  ├─ gui_service_main.c / .h / _config.h
│  └─ README.md
└─ canvas/                           可复用的 Canvas 离屏处理
   ├─ gui_service_canvas.c / .h
   └─ gui_service_canvas_compositor.c / .h
```

- `gui_service.c`：GUI Task 生命周期、LVGL 显示/输入驱动注册，以及 LCD DMA
  刷新桥接。它只编排内部 Module，不持有离屏 Canvas 工作区或启动动画细节。
- `boot/gui_service_boot.c`：启动视觉序列 Module。它在 `ui_init()` 后调用通用 Canvas
  Module 生成并绑定 Boot 的模糊背景，再显式启动 Arc 相位动画。BootReveal 的
  `SCREEN_LOADED` 事件调用 `Service_GUI_Boot_RequestLock()` 时，本 Module 以
  `lv_async_call()` 延后一轮 LVGL 调度，再发起到 Lock 的 Fade；因此不会重入尚未收尾的
  前一次 Screen 切换。
- `canvas/gui_service_canvas.c`：通用 Canvas Module。它独占可复用的 SDRAM 工作区，接收
  调用方给定的图片和模糊半径，返回模糊图像描述符；目前被 Boot 全屏模糊与 Main 局部毛玻璃
  依次复用，后续也可用于壁纸更新和 Settings 局部毛玻璃生成。其隐藏 Canvas 对象挂在 display
  top layer，因而不随短生命周期的 Boot Screen 销毁。`gui_service_canvas_compositor.c` 在调用方提供的长期
  缓冲中执行清晰/模糊帧的矩形、圆角矩形和圆形区域合成；Canvas 不拥有页面级背景。
- `main/gui_service_main.c`：Main Screen 运行时视觉 Module。它仅补齐 SquareLine 未暴露的
  Tabview 内部 Content container 透明 Style，读取布局后的 `MusicModeTabs` 与三颗按钮坐标，
  并持有 Main 的长期 SDRAM 合成背景。它只为显示合成结果而替换 `ui_Main` 的运行时
  Background image；SquareLine 导出对象的 Border、Shadow、Radius 与其他视觉 Style 不由本
  Module 覆盖，它也不在 GUI Task 的逐帧处理路径中重新模糊或合成。

`gui_service_config.h` 保存绘制缓冲行数。改变该值会同时影响 SDRAM 占用、SPI
刷新分块数量和 LVGL 的双缓冲等待行为，必须结合显示帧率与 D-Cache 约束验证。

`boot/gui_service_boot_config.h` 保存启动 Arc 的统一周期、伸缩范围、起始偏移、Boot
背景模糊半径，以及 BootReveal 停留/Fade 时间，允许在不改动画流程的前提下根据真机
观感调节。通用 Canvas Module 不持有 Boot 专属的参数。

## SquareLine 生成目录

`GUI/` 由 SquareLine Studio 1.6.1 生成，是 GUI Service 的只读输入；本 Module
只通过 `GUI/ui.h` 公开的 Screen 和资源符号绑定运行时样式，绝不修改生成的 `.c`、
`.h`、CMake 或资源数组。用户在 SquareLine 完成设计并导出后，再由本 Module 适配。
