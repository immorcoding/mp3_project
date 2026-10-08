# GUI · transport

播放控制与唱盘呈现。

[返回 GUI](gui.md)

### Rules

- **GUI-10** · provisional · 时间、进度与三键仅在 Now Playing；transport 绑定 CLICKED/RELEASED，Task 消费输入；拖动不被 Apply 覆盖，松手不发新命令，进度最迟下一周期回到解码器时间；时间与进度由播放事件驱动，不按墙钟走表；切歌、拔卡与停止态归零。_Why:_ 进度的真实来源是解码器位置，GUI 只消费事件；输入与状态边界不变。_Source:_ [播放控制基线](../../Tools/gui_simulator/scenarios/music_transport.expected)、[音频进度决定](https://github.com/immorcoding/mp3_project/issues/36)、[进度上报与 GUI 通信](https://github.com/immorcoding/mp3_project/issues/37)
- **GUI-14** · provisional · 唱盘底图从 Pack 载入 SDRAM，由 vinyl 合成封面后绑定无事件 Image，图像尺寸与 view 一致；图标复用 LVGL 符号，图片不另编入内部 Flash。_Why:_ 区分资源加载、像素合成和输入，避免重复静态资源。_Source:_ [GUI 模块](../../Service/gui/README.md)、[资源 Service](../../Service/resource/README.md)

### Proposed

- 2026-10-07 · provisional · 唱盘旋转消费 APP 的 playing 与切歌/清空事件，角度由现有 Image 持有，同一 Image 至多一条旋转动画。_Why:_ 播放反馈与播放进度分离，状态所有权单一；接口和周期细节见模块契约。_Source:_ [唱盘模块](../../Service/gui/main/vinyl/README.md)、[采用决定](https://github.com/immorcoding/mp3_project/issues/26)。（合并 inbox 后保留待议；采用 A 实现不自动批准长期强制规则。）

### Signals

- 2026-10-07 · cite · GUI-10 · 唱盘旋转消费 APP 既有 playing 与游标事件，Service 不读游标，旋转不推进播放时间。
- 2026-10-07 · cite · GUI-14 · 旋转沿用 vinyl 合成后的同一 Image 与 SDRAM 缓冲，不新增资源；角度 0 与原基线一致。
