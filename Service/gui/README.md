# GUI Service

本 Module 持有 LVGL v8.3.11 的运行时实例，把 SquareLine 生成的 `GUI/` 静态界面、Platform LCD 的异步矩形写入和 Platform Touch 的轮询式原始触点组合为 GUI Task 可调用的产品级流程。

它不实现 ST7789 命令、SPI DMA、FT6X36 寄存器协议、FMC/SDRAM 初始化，也不创建 FreeRTOS Task；这些职责分别属于 Platform、下层 Component/Adapter 和 APP。

## 公开 Interface

- `Service_GUI_Init(notify_index)`：仅由 GUI Task 调用一次。`notify_index` 是本任务 `GUI_NotifyIndexTypeDef` 给出的 LCD DMA 完成槽。初始化 LVGL，注册 v8 显示驱动与 Pointer 输入驱动，绑定 LCD DMA 最终回调，调用 SquareLine 的 `ui_init()`，再挂上占位色过滤器（生成代码会覆盖 display theme），并对 STARTUP 外观调用 `Service_GUI_ThemeApply()`。
- `Service_GUI_ThemeApply(id)`：仅由同一 GUI Task 调用。切换 Default/Solid 调色板并 invalidate；更新 Boot/BootReveal/Lock/Main 壁纸显隐与 Music Tab 薄层。不扫对象改 hex，不调用 `ui_theme_set()`。运行时切到 Default 时若尚未生成毛玻璃，不会在此处补做 Canvas 模糊。
- `Service_GUI_Process()`：仅在同一 GUI Task 上下文周期调用。按 FreeRTOS Tick 推进 LVGL 时间，并调用 `lv_timer_handler()` 处理刷新、动画和输入。
- `Service_GUI_QueueApply(titles, length, window_index, current_index)`：仅由同一 GUI Task 调用。把一窗曲名填进 Queue 可见行；`length` 为 0 时全部 Hidden，`titles` 可为 NULL。`window_index` 是本窗在播放列表上的起点，用于已有行转 head。`current_index` 是正在播放的播放列表下标；无当前曲时为 `SERVICE_GUI_QUEUE_NO_CURRENT`。不包含 `storage_listbuffer.h`。Label 会拷贝文本。
- `Service_GUI_ConsumeInput(input)`：仅由同一 GUI Task 调用。取走上一圈 `Process()` 记下的一次点击。无点击时 `command` 为 `SERVICE_GUI_INPUT_NONE` 且返回 `SERVICE_OK`，不用 `SERVICE_NOT_READY` 表示空闲。`MUSIC_QUEUE_SELECT` 时 `param` 为播放列表下标；`MUSIC_SEEK` 时为 0..100 百分比。不包含 `storage_listbuffer.h` / `storage_playback_cursor.h`。
- `Service_GUI_TransportApply(playing)`：仅由同一 GUI Task 调用。把 `MusicPlayPauseIcon` 换成 `LV_SYMBOL_PAUSE` 或 `LV_SYMBOL_PLAY`。
- `Service_GUI_ProgressApply(percent)`：仅由同一 GUI Task 调用。把 `MusicPlayingSlider` 设为 0..100；Slider 处于 `PRESSED` 时不覆盖当前拖动。不解码、不真正 seek。
- `Service_GUI_QueueScrollLead()`：仅由同一 GUI Task 调用。返回 `QueueTab` 顶部已滚出的整行数，供窗口协议计算下一窗 `Index`。换窗时从当前 `scroll_y` 扣整行高度，不把列表吸回整页。

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

对外仍只有 `Service_GUI_Init()`、`Service_GUI_Process()`、`Service_GUI_QueueScrollLead()`、`Service_GUI_ConsumeInput()`、`Service_GUI_TransportApply()`、`Service_GUI_ProgressApply()`、`Service_GUI_QueueApply()` 与 `Service_GUI_ThemeApply()`；
以下是 `Service/gui` 内部的实现拆分，不得被 APP 或其他 Service 直接包含或调用。
SquareLine 生成代码唯一允许的例外是由 `GUI/ui_events.h` 声明、GUI Service 实现的
`Service_GUI_Boot_RequestLock()`：它是 BootReveal 的窄事件交接点，不属于供上层调用的
公开 Service Interface，也不要求 `GUI/` 包含任何 Service 头文件。

```text
Service/gui/
├─ gui_service.c / .h / _config.h    GUI Task 生命周期、显示/触摸和 DMA 桥接
├─ gui_service_input.c / .h          点击命令单槽（Process 写入，下一圈 Consume）
├─ theme/                            调色板、占位色过滤器、壁纸/毛玻璃副作用
├─ boot/                             仅属于启动视觉序列
│  ├─ gui_service_boot.c / .h
│  ├─ gui_service_boot_config.h
│  └─ README.md
├─ main/                             Main Screen 的运行时编排
│  ├─ gui_service_main.c / .h
│  ├─ pager/                         循环分页、吸附、重排与圆点动画
│  ├─ queue/                         按 Length 用范本构造生成 Queue 行
│  ├─ transport/                     Now Playing 三键、进度条假 seek 与 PLAY/PAUSE
│  ├─ background/                    壁纸模糊、局部裁剪与 Tabview 兼容
│  └─ README.md
└─ canvas/                           可复用的 Canvas 离屏处理
   ├─ gui_service_canvas.c / .h
   ├─ gui_service_canvas_compositor.c / .h
   └─ README.md
```

- `gui_service.c`：GUI Task 生命周期、LVGL 显示/输入驱动注册，以及 LCD DMA
  刷新桥接。它只编排内部 Module，不持有离屏 Canvas 工作区或启动动画细节。
  在 `ui_init()` 之后调用 `theme/` 挂过滤器（生成代码会先装 basic theme），再对 STARTUP 外观调用 `ThemeApply`。
- `theme/`：持有 Default/Solid 调色板与当前索引。过滤器把五个占位 hex 映射成当前
  RGB；`ThemeApply` 负责 Lock/Main 壁纸显隐和 Solid 下 Music Tab 的半透明 Wash。
  不是状态机，不与电量/时间混装。Boot Arc 动画本轮不改；Solid 下开机页也关壁纸。
- `boot/gui_service_boot.c`：启动视觉序列 Module。它在 `ui_init()` 后调用通用 Canvas
  Module 生成并绑定 Boot 的模糊背景，再显式启动 Arc 相位动画。BootReveal 的
  `SCREEN_LOADED` 事件调用 `Service_GUI_Boot_RequestLock()` 时，本 Module 以
  `lv_async_call()` 延后一轮 LVGL 调度，再发起到 Lock 的 Fade；因此不会重入尚未收尾的
  前一次 Screen 切换。Boot 直接使用共享 Canvas 工作帧作为背景，故启动期间不得再次
  执行 Canvas 模糊；GUI Service 先调用 Main Module 复制其长期模糊壁纸，再调用本
  Module 占用共享工作帧，MainPager 的滚动更新只读取 Main 自己的副本，不会改写 Boot。
- `canvas/gui_service_canvas.c`：通用 Canvas Module。它独占可复用的 SDRAM 工作区，接收
  调用方给定的图片和模糊半径，返回模糊图像描述符；目前被 Boot 全屏模糊与 Main 局部毛玻璃
  依次复用，后续也可用于壁纸更新和 Settings 局部毛玻璃生成。其隐藏 Canvas 对象挂在 display
  top layer，因而不随短生命周期的 Boot Screen 销毁。`gui_service_canvas_compositor.c` 在调用方提供的长期
  缓冲中执行清晰/模糊帧的矩形、圆角矩形和圆形区域合成，也提供严格或带透明越界填充的连续
  图片裁剪；Canvas 不拥有页面级背景。
- `main/gui_service_main.c`：Main Screen 的私有编排入口。它先调用 Pager Module 解析布局、
  定位 MusicPage 至中间物理槽位并绑定循环分页，再调用 Queue 隐藏范本，再绑定 Transport
  三键与进度条，最后调用 Background Module；Solid 下 Background 跳过毛玻璃。入口本身不持有 UI
  状态或离屏图像。
- `main/pager/`：只持有三张 Main Page 的物理槽位、程序化吸附状态和逻辑圆点状态。它监听
  `MainPageContainer` 的 `LV_EVENT_SCROLL_END`，以 50% 阈值吸附至相邻槽位，并在两端轮换既有
  Page 后无动画回中实现循环。它不依赖 Canvas、Platform LCD 或壁纸资源。
- `main/queue/`：只持有 Queue 可见行对象。它把 SquareLine 单行范本的构造序列摘进
  for 循环，按 Length 写入 `QueueTab`，窗口滑动时转 head 改字，并套用当前/非当前
  样式（Border Opa，不改 Width）。点按 `CLICKED` 只刷新行样式并 `post` 选曲命令。
  用 `QueueScrollLead` 报告滚出顶部的整行数。
  不包含 `storage_listbuffer.h` 或 `storage_playback_cursor.h`。
- `main/transport/`：给 Now Playing 三键挂 `CLICKED`、给进度条挂 `RELEASED`，`post` 上一首/播放暂停/下一首/假 seek；
  `TransportApply` 只改播放符号，`ProgressApply` 改进度条（拖动中不写回）。不包含游标头，也不写 SquareLine 事件。
- `main/background/`：只持有 Main 的长期模糊壁纸和 `MusicModeTabs` SDRAM 裁剪背景。它使
  SquareLine 未公开的 Tabview 内部 Content 透明、禁用其横滑，并监听 `MainPageContainer` 的
  `LV_EVENT_SCROLL`，按目标控件当前坐标重裁剪背景。仅 Default 才生成模糊与裁剪图；Solid 不算
  模糊。它不维护分页槽位、吸附或圆点状态。除 `ui_MusicModeTabs` 的运行时 Background image
  外，不覆盖 SquareLine 导出对象的视觉 Style。

`gui_service_config.h` 保存绘制缓冲行数。改变该值会同时影响 SDRAM 占用、SPI
刷新分块数量和 LVGL 的双缓冲等待行为，必须结合显示帧率与 D-Cache 约束验证。

`boot/gui_service_boot_config.h` 保存启动 Arc 的统一周期、伸缩范围、起始偏移、Boot
背景模糊半径，以及 BootReveal 停留/Fade 时间，允许在不改动画流程的前提下根据真机
观感调节。通用 Canvas Module 不持有 Boot 专属的参数。

## SquareLine 生成目录

`GUI/` 由 SquareLine Studio 1.6.1 生成，是 GUI Service 的只读输入；本 Module
只通过 `GUI/ui.h` 公开的 Screen 和资源符号绑定运行时样式，绝不修改生成的 `.c`、
`.h`、CMake 或资源数组。用户在 SquareLine 完成设计并导出后，再由本 Module 适配。
