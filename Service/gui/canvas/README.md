# GUI Canvas 离屏处理 Module

本 Module 为 `Service/gui` 内部的运行时视觉效果提供一块可复用的全屏离屏工作区和
局部图像处理 Interface。它不创建可见页面、不维护业务状态，也不属于 APP 或其他 Service
可调用的公开 Interface。

当前包含两部分：

- `gui_service_canvas.c / .h`：持有唯一的全屏 LVGL Canvas 工作区，执行图片复制与横向、
  纵向软件模糊；
- `gui_service_canvas_compositor.c / .h`：对调用方持有的图片缓冲执行严格裁剪、带透明
  越界填充的裁剪，以及清晰/模糊区域合成。

## 资源与生命周期

`service_gui_effect_canvas_buffer` 是位于 `.sdram_framebuffer` 的全屏 Canvas 工作缓冲，按
`LV_IMG_CF_TRUE_COLOR_ALPHA` 的最大单帧容量预留。隐藏 Canvas 对象挂在 LVGL display 的 top
layer，因此不随 Boot 这类临时 Screen 销毁；但它不参与可见层级绘制。实际调用时 Canvas 沿用
输入图片的颜色格式，因此工作区的额外容量不改变输出图片格式。

`service_gui_canvas_blur_image()` 返回的描述符和像素指向这块**共享工作区**。下一次调用
该函数会覆盖前一次输出，因此调用方必须按用途选择生命周期：

- Boot 仅在启动阶段显示，可直接绑定共享模糊帧；Boot 显示期间不得再次执行 Canvas 模糊；
- Main 需要在后续横滑事件中继续读取模糊像素，必须立即复制到 `main/` 自己持有的全屏
  SDRAM 缓冲；MainPager 滚动只裁剪该副本，不会改写 Canvas 工作区；
- 未来 Settings、Books 或壁纸切换若需长期效果，也必须各自持有输出缓冲，不能长期引用
  Canvas 返回的描述符。

所有 Canvas 和 LVGL Interface 均只能在 GUI Task 中调用，不能在 ISR 或其他 FreeRTOS Task
中操作。

## 图片格式与内存约束

`service_gui_canvas_blur_image()` 当前接受尺寸恰为显示屏、`data_size` 完整的
`LV_IMG_CF_TRUE_COLOR` 或 `LV_IMG_CF_TRUE_COLOR_ALPHA` 图片，并返回与输入同格式的模糊帧。
LVGL v8 的 `lv_canvas_set_buffer()` 不会维护内部描述符的 `data_size`，因此本 Module 在返回给
调用方前补齐该字段。

裁剪与合成 Interface 目前仍只接受 `LV_IMG_CF_TRUE_COLOR_ALPHA`：带透明越界填充的裁剪需要
Alpha 表示图像外的透明像素，局部毛玻璃的当前壁纸资源也使用该格式。若以后要将 RGB565
不透明壁纸接入局部玻璃路径，必须先扩展并验证合成/裁剪 Interface，而不能只依赖 Canvas
模糊入口已经接受 `LV_IMG_CF_TRUE_COLOR`。

Canvas 工作缓冲按当前 `240 x 320` 显示规格分配，约为 `225 KiB`。首地址按
`PLATFORM_DMA_BUFFER_ALIGNMENT` 对齐；它是 CPU Canvas 操作的工作区，页面级输出缓冲由各
调用 Module 自行配置和统计。

## 裁剪与合成 Interface

- `service_gui_canvas_extract_image_region()`：严格裁剪；`source_area` 必须完整位于源图片
  内，适合复制完整模糊帧或确认不会越界的区域；
- `service_gui_canvas_extract_image_region_padded()`：输出大小保持为请求区域大小，源图外的
  像素填充为全零透明，适合对象在横滑中部分离开屏幕时保持全局背景坐标；
- `service_gui_canvas_compose_blurred_regions()`：将完整清晰图与完整模糊图按矩形、圆角矩形
  或圆形区域合成为调用方持有的长期背景。当前 Music 未使用该全图合成路径，保留给后续
  多卡片局部模糊等经审校的需求。

裁剪和合成的输入、输出像素缓冲不得重叠。Interface 不负责对象坐标、页面生命周期或将图片
绑定到某个 SquareLine 对象；这些属于 `boot/`、`main/` 等调用 Module。

## 当前调用顺序

```text
Service_GUI_Init()
  -> ui_init()
  -> main/service_gui_main_prepare()
       -> Canvas 模糊当前壁纸
       -> Main 复制长期全屏模糊帧并建立 MusicModeTabs 首帧裁剪
  -> boot/gui_service_boot_prepare_background()
       -> Canvas 重新模糊当前壁纸
       -> Boot 直接绑定共享工作帧
  -> boot/service_gui_boot_start()
```

该顺序保证 Main 的长期数据不会被 Boot 覆盖，同时 Boot 在启动阶段拥有最后生成的共享模糊
背景。`Service_GUI_Process()` 的 MainPager 滑动事件不会重新模糊整张壁纸。

## 维护边界

- 不修改 `GUI/` 或 SquareLine 工程；
- 不包含 STM32 HAL、SPI、DMA2D、SDRAM 初始化或 FreeRTOS 调度逻辑；
- 不持有 Boot、Main、Settings 等页面对象，也不决定视觉区域的位置、圆角或样式；
- 新增长期 Canvas 效果前，先明确输出缓冲归属、峰值 SDRAM、共享工作区覆盖时机和 D-Cache
  约束，再经用户审校。
