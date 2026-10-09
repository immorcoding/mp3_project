# GUI

界面所有权、交互边界与视觉效果生命周期。

Next id: GUI-21

## Pillars

- 对象树只有一个事实源，行为通过句柄协作。
- 视觉连续性与资源生命周期共同决定交互方案。
- PC 像素等价不替代板级触摸与性能验收。

## Open questions

- 过渡说明（2026-10-09）：GUI-10、GUI-17、GUI-19、GUI-20 描述的是接入播放快照后的目标态（[0.6.0 地图](https://github.com/immorcoding/mp3_project/issues/44)）；交互 spec 实施前，现有音乐页仍按假进度、本地 playing 与直接写游标运行，审阅不据此判违规。实施完成后删除本条。
- Library、Album Detail/Mini Player、Books/Reader、Settings 子页何时进入实施 spec？现有占位不代表已实现；阅读横滑与 Main 手势、阅读字号与系统字号须分开决定。
- 可换壁纸、按内容版本的 Blur 缓存及 Settings 合成背景何时实施？先确认缓存键、失效、峰值内存和真机耗时，不把当前 Alpha 原型视为 RGB565 持久缓存。
- ID3 封面何时接入？0.6.0 不做。时间、进度与三态由播放快照驱动，见 [transport](gui.transport.md)。
- 安全锁、背光与唤醒何时设计？Lock 当前仅视觉锁屏，真实亮度需 Platform LCD/PWM 能力。
- 何时升级 LVGL 9 并评估 XML 编辑器？随 Vendor 升级再决定，ADR-0016 当前未采用。

## Titles

- [ownership](gui.ownership.md): 对象、设计变更与行为模块的职责。
- [appearance](gui.appearance.md): 颜色来源、外观与系统背景。
- [navigation](gui.navigation.md): 普通页面结构与横向手势。
- [queue](gui.queue.md): 窗口交接与播放列表行的稳定呈现。
- [transport](gui.transport.md): 播放控制与唱盘呈现。
- [effects](gui.effects.md): 离屏缓冲所有权、启动次序与局部玻璃。
- [lock](gui.lock.md): 待机视觉层级与解锁提示。
- [regression](gui.regression.md): 视觉变化的验收依据。
