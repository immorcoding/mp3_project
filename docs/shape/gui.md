# GUI

界面所有权、交互边界与视觉效果生命周期。

Next id: GUI-19

## Pillars

- 对象树只有一个事实源，行为通过句柄协作。
- 视觉连续性与资源生命周期共同决定交互方案。
- PC 像素等价不替代板级触摸与性能验收。

## Open questions

- Library、Album Detail/Mini Player、Books/Reader、Settings 子页何时进入实施 spec？现有占位不代表已实现；阅读横滑与 Main 手势、阅读字号与系统字号须分开决定。
- 可换壁纸、按内容版本的 Blur 缓存及 Settings 合成背景何时实施？先确认缓存键、失效、峰值内存和真机耗时，不把当前 Alpha 原型视为 RGB565 持久缓存。
- 唱盘旋转、ID3 封面、真实时间和音频进度何时接入？先确认 playing/paused/CLEAR 行为；当前假状态不能充当解码进度。
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
