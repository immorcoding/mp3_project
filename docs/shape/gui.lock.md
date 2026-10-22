# GUI · lock

待机视觉层级与解锁提示。

[返回 GUI](gui.md)

### Rules

- **GUI-15** · provisional · Lock 根处理上滑；大时间、短日期与值一致的电量组形成上部层级，底部文字/Home indicator 同组低频透明度呼吸且不承接事件，不加阴影或额外 Gesture Bubble；动画生命周期跟随屏幕：卸载时删除、加载时重建。_Why:_ 小屏保持单一手势锚点；既有触摸问题来自采样率，非布局容器。_Source:_ [Boot/Lock 基线](../../Tools/gui_simulator/scenarios/boot_lock.expected)

### Signals

- 2026-10-22 · cite · GUI-15 · [#106](https://github.com/immorcoding/mp3_project/issues/106)：呼吸动画随 Lock 屏幕卸载停止、加载时重建，外观、时长、曲线不变；模拟器 `lock_anim` 场景以 `--anims` 断言 Lock 时 1、解锁后 0、回到 Lock 后 1。
