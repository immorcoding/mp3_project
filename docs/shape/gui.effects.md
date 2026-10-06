# GUI · 效果与生命周期

[返回 GUI](gui.md) · [现行界面](gui.design.md)。修改启动时序、壁纸、Canvas 或局部玻璃时读取本参考；API 与私有模块入口见 [Service/gui](../../Service/gui/README.md)。

## Boot 时序

Boot/BootReveal 是一次性 Screen；view 创建对象/静态样式，boot 管时序。Orbital loader 使用普通 Arc（非 Spinner）：暗色完整 MAIN 轨道、Accent INDICATOR、透明 KNOB 与 LOADING 标签。线性相位使活动弧在 30°–210° 间伸缩，两端均不倒退、跨周期无跳变；相位时长和起始偏移以 boot 配置为准。

```text
Boot → 保持与淡出 → BootReveal
BootReveal SCREEN_LOADED → lv_async_call → 延后一轮 GUI/LVGL 调度 → Lock
```

当前 Boot 保持 4000 ms、淡出 400 ms；Reveal 保持 200 ms、淡出 600 ms，配置位于 `Service/gui/boot/gui_service_boot_config.h`。修改感受先同步设计，再由 boot_lock 场景验证。BootReveal SCREEN_LOADED 仍处前一切屏收尾，直接嵌套 lv_scr_load_anim 会取消/强制完成旧动画，因此必须异步交接。切屏只在 GUI Task 内，FreeRTOS Timer/ISR 不操作 LVGL。

view 创建时已加载 Boot，不能依赖后来才注册的首次 SCREEN_LOADED 启动动画。Init 按以下顺序显式准备：

1. Main 布局与 Pager 回中完成；Default 先在共享 Canvas 生成模糊图并立即复制为 Main 长期背景。
2. Default 再用同一 Canvas 生成 Boot 模糊图，直接绑定 Boot，Boot 成为工作区最后使用者。
3. 显式启动 boot 动画，并重对 lv_tick，避免初始化墙钟被计入切屏延迟。

淡出后 LVGL 删除 Boot，view 将句柄置空；Arc 销毁时其动画自动删除。BootReveal 只做短视觉过渡，无输入或业务控件。Default 的 Boot 用模糊根 Background image，BootReveal/Lock/Main 共用清晰系统壁纸；Solid 全部关闭壁纸并铺 Ground。正常流程不重新进入 Boot。

## Canvas 的所有权

`canvas/` 是通用离屏处理，隐藏对象挂 display top layer，不挂 Boot；Boot 销毁后仍可复用。大像素缓冲 `service_gui_effect_canvas_buffer` 静态位于 SDRAM，不用 LVGL/RTOS 小堆或未审计 malloc，也不放 DTCM。GUI Task 同时只允许一个离屏任务占用。

Canvas 输出描述符仅在下一次处理前有效。长期消费者必须复制或取得独占；Main 立即复制，Boot 在显示期间直接引用，期间禁止其他模糊改写共享区。Canvas 不进入可见对象层级，也不能把同一可覆盖输出绑定给多张长期卡片。

当前对 LV_IMG_CF_TRUE_COLOR_ALPHA 源执行水平/垂直 LVGL Canvas blur，工作帧 240 × 320 × 3 = 230400 B。DMA2D 可做转换/拷贝/填充/Alpha，不做模糊卷积；若效果/性能不足，才评审滑动窗口多次 Box Blur。DMA 写入源在 CPU 读取前 Invalidate；CPU 输出被 DMA 读取前 Clean；同一 CPU 缓存域软件渲染不额外清理。

## Music 局部玻璃

仅 Default 建立 MusicModeTabs 的整块圆角玻璃（含标签与内容）；Solid 只用 Wash，不生成模糊。主壁纸始终清晰，播放器按钮不分配玻璃缓冲。

Main 长期持有完整 Blur 副本及一张 Tabview 裁剪输出。布局计算与回中后，按 MusicModeTabs 相对 Main 根的实际坐标裁剪，而非复制 view 百分比为固定像素；横滑 SCROLL 每次重新取坐标并复制像素，越界输出透明。输出尺寸与对象一致，LVGL 圆角背景绘制避免方块边缘。

`main/background` 只替换 MusicModeTabs 的 Background image 与背景 Opa；内部 Content 由 view 保持背景、背景图、边框、轮廓、阴影透明并禁止横向滚动。背景模块不覆盖其他对象样式。

全屏模糊仅在壁纸改变时重建，不放入动画、文字更新或每次 Process；滚动只从长期 Blur 副本裁剪。现有静态缓冲按整屏上限预留：共享工作区 + Main Blur + 最大裁剪图，Alpha 峰值约 675 KiB，实际目标略矮；容量源在 `main/background/gui_service_main_background_config.h`。view 布局变更于下一次初始化重建裁剪。

后续页面可复用裁剪算法，但各自管理背景与更新时间，不能不加管理地共用 Music 输出。新增页面玻璃须先审校设计；Settings 的另一种合成模型见[待实施设计](gui.planned.md)。
