# GUI Service

本 Module 持有 LVGL v8.3.11 的运行时实例，把私有 `view/` 手写的界面、Platform LCD 的异步矩形写入和 Platform Touch 的轮询式原始触点组合为 GUI Task 可调用的产品级流程。界面不使用图形化生成器（ADR-0016）。

它不实现 ST7789 命令、SPI DMA、FT6X36 寄存器协议、FMC/SDRAM 初始化，也不创建 FreeRTOS Task；这些职责分别属于 Platform、下层 Component/Adapter 和 APP。

## 公开 Interface

- `Service_GUI_Init(notify_index)`：仅由 GUI Task 调用一次。`notify_index` 是本任务 `GUI_NotifyIndexTypeDef` 给出的 LCD DMA 完成槽。初始化 LVGL，注册 v8 显示驱动与 Pointer 输入驱动，绑定 LCD DMA 最终回调；以 STARTUP 外观初始化共享颜色 style，由 `view/` 创建全部 Screen 并加载 Boot，再应用 Screen 外观、准备 Main 与 Boot。
- `Service_GUI_ThemeApply(id)`：仅由同一 GUI Task 在 `Init()` 成功后调用，之前返回 `SERVICE_NOT_READY`；启动序列未结束（Boot 仍存在）时返回 `SERVICE_BUSY`，因为 Boot 可能正显示共享 Canvas 工作帧。首次生成毛玻璃失败时回滚到原外观。切换 Default/Solid 调色板，刷新共享颜色 style 与 BootReveal/Lock/Main 的壁纸或 Ground，再设置 Music Tab 毛玻璃或薄层；首次切到 Default 时按需生成长期模糊壁纸。
- `Service_GUI_Process()`：仅在同一 GUI Task 上下文周期调用。按 FreeRTOS Tick 推进 LVGL 时间，并调用 `lv_timer_handler()` 处理刷新、动画和输入。
- `Service_GUI_QueueApply(titles, length, window_index, current_index)`：仅由同一 GUI Task 调用。把一窗曲名填进 Queue 可见行；`length` 为 0 时全部 Hidden，`titles` 可为 NULL。`window_index` 是本窗在播放列表上的起点，用于已有行转 head。`current_index` 是正在播放的播放列表下标；无当前曲时为 `SERVICE_GUI_QUEUE_NO_CURRENT`。不包含 `storage_listbuffer.h`。Label 会拷贝文本。
- `Service_GUI_ConsumeInput(input)`：仅由同一 GUI Task 调用。取走上一圈 `Process()` 记下的一次点击。无点击时 `command` 为 `SERVICE_GUI_INPUT_NONE` 且返回 `SERVICE_OK`，不用 `SERVICE_NOT_READY` 表示空闲。`MUSIC_QUEUE_SELECT` 时 `param` 为播放列表下标；`MUSIC_SEEK` 时为 0..100 百分比。不包含 `storage_listbuffer.h` / `storage_playback_cursor.h`。
- `Service_GUI_TransportApply(playing)`：仅由同一 GUI Task 调用。把 `MusicPlayPauseIcon` 换成 `LV_SYMBOL_PAUSE` 或 `LV_SYMBOL_PLAY`。
- `Service_GUI_ProgressApply(percent)`：仅由同一 GUI Task 调用。把 `MusicPlayingSlider` 设为 0..100；Slider 处于 `PRESSED` 时不覆盖当前拖动。不解码、不真正 seek。
- `Service_GUI_VinylApply(playing, reset_angle)`：仅由同一 GUI Task 调用。`playing` 为真时唱盘从当前角度顺时针匀速续转（`SERVICE_GUI_MAIN_VINYL_REVOLUTION_MS`，6 s 一圈），为假时停在当前角度；`reset_angle` 为真先把角度归零，用于切歌与清空播放内容。调用方在 playing 变化、游标变化或 CLEAR 时调用，Service 不读播放游标；重复调用不叠加动画。
- `Service_GUI_QueueScrollLead()`：仅由同一 GUI Task 调用。返回 `QueueTab` 顶部已滚出的整行数，供窗口协议计算下一窗 `Index`。换窗时从当前 `scroll_y` 扣整行高度，不把列表吸回整页。

重复调用 `Service_GUI_Init()` 返回 `SERVICE_BUSY`。当前 GUI Task 将初始化失败视为致命并调用
`Error_Handler()`；GUI Service 尚未提供部分初始化后的回滚或重试 Interface。

## 编译期依赖

- LVGL v8.3.11 的公开入口 `lvgl.h`；
- `Platform/lcd`、`Platform/touch` 的公开 Interface；
- FreeRTOS 的任务通知和 Tick Interface。

本 Module 不包含 STM32 HAL、CubeMX 外设 Handle 或 LCD/触摸的 Adapter 头文件。

## 资源与运行时路径

两块完整 RGB565 绘制缓冲由本 Module 静态持有，并通过 `.sdram_framebuffer` 放入外部 SDRAM。缓冲首地址按 `PLATFORM_DCACHE_LINE_SIZE` 对齐；SPI DMA 发起前的 D-Cache Clean 仍由下层 LCD Adapter 负责。

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

对外仍只有 `Service_GUI_Init()`、`Service_GUI_Process()`、`Service_GUI_QueueScrollLead()`、`Service_GUI_ConsumeInput()`、`Service_GUI_TransportApply()`、`Service_GUI_ProgressApply()`、`Service_GUI_VinylApply()`、`Service_GUI_QueueApply()` 与 `Service_GUI_ThemeApply()`；
以下是 `Service/gui` 内部的实现拆分，不得被 APP 或其他 Service 直接包含或调用。

```text
Service/gui/
├─ gui_service.c / .h / _config.h    GUI Task 生命周期、显示/触摸和 DMA 桥接、ThemeApply 编排
├─ gui_service_input.c / .h          点击命令单槽（Process 写入，下一圈 Consume）
├─ view/                             手写界面：创建全部 Screen、静态样式、对象句柄、默认壁纸
│  ├─ gui_service_view.c / .h        创建顺序、句柄、壁纸入口
│  ├─ gui_service_view_boot.c        Boot / BootReveal
│  ├─ gui_service_view_lock.c        Lock（含上滑解锁与呼吸提示）
│  ├─ gui_service_view_main.c        Main 外壳：状态栏、分页视口、三张内容页、圆点
│  ├─ gui_service_view_music.c       Music 标签视图：Now Playing、Queue 容器、Library
│  ├─ gui_service_view_wallpaper*    默认壁纸描述符与固件 SDRAM 像素区
│  └─ README.md
├─ theme/                            调色板角色与共享颜色 style、Screen 壁纸/Ground
├─ boot/                             启动视觉序列：Boot → BootReveal → Lock
│  ├─ gui_service_boot.c / .h
│  ├─ gui_service_boot_config.h
│  └─ README.md
├─ main/                             Main Screen 的运行时编排
│  ├─ gui_service_main.c / .h
│  ├─ pager/                         循环分页、吸附、重排与圆点动画
│  ├─ queue/                         按 Length 构造 Queue 行并滑窗复用
│  ├─ transport/                     Now Playing 三键、进度条假 seek 与 PLAY/PAUSE
│  ├─ vinyl/                         假唱盘第一帧绑到唱盘 Image，并按 playing 旋转
│  ├─ background/                    壁纸模糊、局部裁剪；Solid 薄层
│  └─ README.md
└─ canvas/                           可复用的 Canvas 离屏处理
   ├─ gui_service_canvas.c / .h
   ├─ gui_service_canvas_compositor.c / .h
   ├─ gui_service_canvas_config.h
   └─ README.md
```

- `gui_service.c`：GUI Task 生命周期、LVGL 显示/输入驱动注册，以及 LCD DMA
  刷新桥接。它只编排内部 Module，不持有离屏 Canvas 工作区或启动动画细节。Init 顺序为：
  共享颜色 style → `view/` 创建界面 → Screen 外观 → Main → Boot 背景 → Boot 序列。
  `Service_GUI_ThemeApply()` 也在这里编排：选调色板 → 刷新 style 与 Screen → Main 外观。
- `view/`：唯一创建界面对象的 Module，发布 `Service_GUI_ViewTypeDef` 句柄供其他私有
  Module 使用；颜色只按角色引用 `theme/` 的共享 style。Boot 切走后被删除，其句柄回到 NULL。
  详见 [view/README.md](view/README.md)。
- `theme/`：`gui_service_theme.c` 持有 Default/Solid 调色板、当前索引和按角色取色（不含
  LVGL，主机测试覆盖）；`gui_service_theme_style.c` 持有 5 × 5 个只含一项颜色的共享
  style，切外观时刷新颜色并 `lv_obj_report_style_change()`，同时设置 Screen 壁纸或 Ground。
  不是状态机，不与电量/时间混装。
- `boot/gui_service_boot.c`：启动视觉序列 Module。Default 下调用通用 Canvas Module 生成并
  绑定 Boot 的模糊背景；`service_gui_boot_start()` 排定 Boot 保持后淡出到 BootReveal（Boot 随切换
  结束被 LVGL 删除），并启动 Arc 相位动画。BootReveal 的 `SCREEN_LOADED` 回调以
  `lv_async_call()` 延后一轮 LVGL 调度，再发起到 Lock 的 Fade；因此不会重入尚未收尾的
  前一次 Screen 切换。Boot 直接使用共享 Canvas 工作帧作为背景，故启动期间不得再次
  执行 Canvas 模糊；GUI Service 先调用 Main Module 复制其长期模糊壁纸，再调用本
  Module 占用共享工作帧，MainPager 的滚动更新只读取 Main 自己的副本，不会改写 Boot。
- `canvas/gui_service_canvas.c`：通用 Canvas Module。它独占可复用的全屏 SDRAM 模糊工作区，另持有
  Now Playing 假唱盘独立缓冲。模糊接收调用方给定的图片和半径；假唱盘由 compositor 写入纯色圆。
  隐藏 Canvas 对象挂在 display top layer，不随 Boot Screen 销毁。`gui_service_canvas_compositor.c`
  提供裁剪、毛玻璃区域合成和纯色圆合成；Canvas 不把图片绑到界面对象。
- `main/gui_service_main.c`：Main Screen 的私有编排入口。它先调用 Pager Module 解析布局、
  定位 MusicPage 至中间物理槽位并绑定循环分页，再让 Queue 打开 QueueTab 滚动，再绑定 Transport
  三键与进度条，再绑假唱盘第一帧，最后调用 Background Module。`service_gui_main_apply_theme()`
  供 ThemeApply 更新 Music 毛玻璃或薄层。入口本身不持有 UI 状态或离屏图像。
- `main/pager/`：只持有三张 Main Page 的物理槽位、程序化吸附状态和逻辑圆点状态。它监听
  `MainPageContainer` 的 `LV_EVENT_SCROLL_END`，以 50% 阈值吸附至相邻槽位，并在两端轮换既有
  Page 后无动画回中实现循环。它不依赖 Canvas、Platform LCD 或壁纸资源。
- `main/queue/`：只持有 Queue 可见行对象。行工厂按 Length 在 `QueueTab` 中构造行，窗口滑动时
  转 head 改字，并套用当前/非当前样式（Border Opa、曲名颜色角色，不改 Width）。点按
  `CLICKED` 只刷新行样式并 `post` 选曲命令。用 `QueueScrollLead` 报告滚出顶部的整行数。
  不包含 `storage_listbuffer.h` 或 `storage_playback_cursor.h`。
- `main/transport/`：给 Now Playing 三键挂 `CLICKED`、给进度条挂 `RELEASED`，`post` 上一首/播放暂停/下一首/假 seek；
  `TransportApply` 只改播放符号，`ProgressApply` 改进度条（拖动中不写回）。不包含游标头。
- `main/vinyl/`：把 Canvas 假唱盘第一帧绑到唱盘 Image，并按 `VinylApply` 用一条以该 Image 为 var 的 lv_anim
  绕圆心顺时针旋转；暂停删动画保留角度，切歌/清空归零。角度只存在 lv_img 对象里，不读游标、不按墙钟走表。
- `main/background/`：只持有 Main 的长期模糊壁纸和 `MusicModeTabs` SDRAM 裁剪背景。Default
  下首次需要时生成模糊壁纸，并监听 `MainPageContainer` 的 `LV_EVENT_SCROLL`，按目标控件当前坐标
  重裁剪背景；Solid 下改为半透明 Wash 薄层、跳过裁剪。它不维护分页槽位、吸附或圆点状态；除
  `MusicModeTabs` 的背景源与背景 Opa 外，不覆盖 `view/` 创建对象的视觉 Style。

`gui_service_config.h` 保存绘制缓冲行数。改变该值会同时影响 SDRAM 占用、SPI
刷新分块数量和 LVGL 的双缓冲等待行为，必须结合显示帧率与 D-Cache 约束验证。

`boot/gui_service_boot_config.h` 保存启动 Arc 的统一周期、伸缩范围、起始偏移、Boot
背景模糊半径、Boot 保持与淡出时间，以及 BootReveal 停留/Fade 时间，允许在不改动画流程
的前提下根据真机观感调节。通用 Canvas Module 不持有 Boot 专属的参数。

## 验证

- 主机测试：`Tests/gui_theme`（调色板）、`Tests/gui_canvas`（合成）、`Tests/gui_task`（输入单槽、窗口协议、Transport）。
- 界面等价性：`./Tools/gui_simulator/run-scenarios.ps1` 在 PC 上原样运行本 Module，逐帧比对场景基线；见 [Tools/gui_simulator/README.md](../../Tools/gui_simulator/README.md)。
- 板级：显示刷新、DMA、触摸与真实帧率仍须上板（`hw:pending`）。
