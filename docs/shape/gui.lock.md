# GUI · lock

待机视觉层级与解锁提示。

[返回 GUI](gui.md)

### Rules

- **GUI-15** · provisional · Lock 根处理上滑；大时间、短日期与值一致的电量组形成上部层级，底部文字/Home indicator 同组低频透明度呼吸且不承接事件，不加阴影或额外 Gesture Bubble。_Why:_ 小屏保持单一手势锚点；既有触摸问题来自采样率，非布局容器。_Source:_ [Boot/Lock 基线](../../Tools/gui_simulator/scenarios/boot_lock.expected)
