# Boot GUI 启动视觉 Module

本 Module 只负责一次性启动视觉序列：Boot 模糊壁纸、运行时 Arc 动画，以及从
BootReveal 异步淡出到 Lock Screen 的交接。它是 `Service/gui` 的私有生命周期 Module，
不维护时间、电量、音乐或触摸业务状态，也不向 APP 或其他 Service 暴露 Interface。

## SquareLine 对象与窄事件入口

本 Module 只读取 SquareLine 从 `GUI/ui.h` 导出的对象：

- `ui_Boot`：运行时模糊壁纸绑定目标；
- `ui_BootOrbitRing`：完整轨道与活动弧的运行时动画目标；
- `ui_BootReveal`：SquareLine 的中间过渡 Screen；
- `ui_Lock`：启动序列的最终 Screen。

`Service_GUI_Boot_RequestLock()` 是唯一由 SquareLine 生成事件代码按符号调用的函数。它对应
`ui_BootReveal` 的 `SCREEN_LOADED`，属于 GUI Service 内部的窄事件交接点，不是公开 Service
Interface，也不要求 `GUI/` 包含任何 Service 头文件。

## 背景与 Canvas 生命周期

`service_gui_boot_prepare_background()` 在 **Default** 下调用 `canvas/` 生成当前壁纸的模糊帧，并将返回的
共享 Canvas 描述符直接绑定到 `ui_Boot` 的 Background image。该描述符只在下一次 Canvas
模糊操作前有效。**Solid** 跳过模糊，沿用 `ThemeApply` 铺上的 Ground，不再写背景图。

当前初始化顺序固定为：

```text
ui_init()
  -> main/service_gui_main_prepare()
       Main 复制自己的长期全屏模糊壁纸
  -> service_gui_boot_prepare_background()
       Boot 直接绑定共享 Canvas 工作帧
  -> service_gui_boot_start()
```

因此，Boot 显示期间不得再次调用 `service_gui_canvas_blur_image()`；否则共享工作帧被覆盖，
Boot 背景会改变。MainPager 后续横滑只读取 Main 自己的副本，不会影响 Boot 的启动画面。

## Arc 动画

`service_gui_boot_start()` 为 `ui_BootOrbitRing` 创建一个无限循环的统一相位动画。回调依据
当前相位同时计算活动弧的起止角度：活动弧先伸长、再缩短，前端连续追上后端并回到起点，
避免两个独立动画在缩短阶段产生反向回弹或相位漂移。

周期、最小/最大 Sweep、起始偏移和背景模糊半径全部位于
`gui_service_boot_config.h`。其中 Arc 偏移遵循 LVGL 的顺时针增角定义；调整视觉时先改配置，
不要在 SquareLine 生成代码中补动画逻辑。

## Screen 交接

Boot 的 Screen 切换由 SquareLine 导出的事件与本 Module 分工完成：

```text
Boot 动画完成
  -> SquareLine 切至 BootReveal
  -> BootReveal SCREEN_LOADED
  -> Service_GUI_Boot_RequestLock()
  -> lv_async_call() 延后一轮 LVGL 调度
  -> Fade 到 ui_Lock
```

异步交接避免在前一段 Screen 切换尚未收尾时嵌套调用 `lv_scr_load_anim()`。内部标志位确保
重复事件不会叠加多次切换。Boot 是正常产品流程中的一次性 Screen，不设计重新进入。

## 维护边界

- 只允许 GUI Task 调用 LVGL Interface；ISR 和其他 Task 不得调用本 Module；
- 不修改 `GUI/` 的生成文件、SquareLine 的 Screen 切换动作或静态视觉 Style；
- 不承担真实背光渐亮、可换壁纸缓存、音乐暂停或 Lock Screen 运行时数据；
- 若未来在 Boot 显示期间新增 Canvas 效果，必须先改为独立输出缓冲或重新设计共享工作区
  生命周期，并经用户审校。
