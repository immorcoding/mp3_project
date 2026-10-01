# Boot GUI 启动视觉 Module

本 Module 负责一次性启动视觉序列：Boot 模糊壁纸、运行时 Arc 动画，以及 Boot → BootReveal →
Lock 的切屏时序。它是 `Service/gui` 的私有生命周期 Module，不维护时间、电量、音乐或触摸业务
状态，也不向 APP 或其他 Service 暴露 Interface。对象由 `view/` 创建，本 Module 经句柄访问：

- `boot.screen`：运行时模糊壁纸绑定目标；
- `boot.orbit_ring`：完整轨道与活动弧的运行时动画目标；
- `boot_reveal`：清晰壁纸的中间过渡 Screen；
- `lock`：启动序列的最终 Screen。

## 背景与 Canvas 生命周期

`service_gui_boot_prepare_background()` 在 **Default** 下调用 `canvas/` 生成当前壁纸的模糊帧，并将返回的
共享 Canvas 描述符直接绑定到 Boot 的 Background image。该描述符只在下一次 Canvas
模糊操作前有效。**Solid** 跳过模糊，沿用 theme/ 铺上的 Ground，不再写背景图。

当前初始化顺序固定为：

```text
view/ 创建界面（加载 Boot）
  -> main/service_gui_main_prepare()
       Main 复制自己的长期全屏模糊壁纸（仅 Default）
  -> service_gui_boot_prepare_background()
       Boot 直接绑定共享 Canvas 工作帧（仅 Default）
  -> service_gui_boot_start()
```

因此，Boot 显示期间不得再次调用 `service_gui_canvas_blur_image()`；否则共享工作帧被覆盖，
Boot 背景会改变。MainPager 后续横滑只读取 Main 自己的副本，不会影响 Boot 的启动画面。
Solid 上电后运行时切到 Default 时，Boot 从未绑定共享工作帧，main/background 按需模糊是安全的。

## Arc 动画

`service_gui_boot_start()` 为启动环创建一个无限循环的统一相位动画。回调依据当前相位同时计算
活动弧的起止角度：活动弧先伸长、再缩短，前端连续追上后端并回到起点，避免两个独立动画在缩短
阶段产生反向回弹或相位漂移。

周期、最小/最大 Sweep、起始偏移、背景模糊半径与各段时长全部位于 `gui_service_boot_config.h`。
其中 Arc 偏移遵循 LVGL 的顺时针增角定义；调整视觉时先改配置。

## Screen 交接

```text
service_gui_boot_start()
  -> lv_scr_load_anim(BootReveal, FADE_OUT, REVEAL_FADE, HOLD, auto_del)
       Boot 保持 SERVICE_GUI_BOOT_HOLD_TIME_MS 后淡出；切换结束由 LVGL 删除 Boot
  -> BootReveal SCREEN_LOADED（本 Module 注册的回调）
  -> lv_async_call() 延后一轮 LVGL 调度
  -> Fade 到 Lock（停留 REVEAL_HOLD，淡出 LOCK_FADE）
```

异步交接避免在前一段 Screen 切换尚未收尾时嵌套调用 `lv_scr_load_anim()`。内部标志位确保
重复事件不会叠加多次切换。Boot 是正常产品流程中的一次性 Screen，不设计重新进入；删除后
`view/` 把其句柄置空，ThemeApply 等调用方按 NULL 跳过。

## 维护边界

- 只允许 GUI Task 调用 LVGL Interface；ISR 和其他 Task 不得调用本 Module；
- 不修改 `view/` 定义的静态视觉 Style；
- 不承担真实背光渐亮、可换壁纸缓存、音乐暂停或 Lock Screen 运行时数据；
- 若未来在 Boot 显示期间新增 Canvas 效果，必须先改为独立输出缓冲或重新设计共享工作区
  生命周期，并经用户审校。
