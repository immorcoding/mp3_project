# GUI · transport

播放控制与唱盘呈现。

[返回 GUI](gui.md)

### Rules

- **GUI-10** · provisional · 时间、进度与三键仅在 Now Playing；三键发 PREV/TOGGLE/NEXT，连按不合并，GUI 不持 playing 与游标，三态与曲目按 GUI-17 采信；时间 Label 显示“已解码/总时长”，总时长未知写 `--:--`、进度条停 0；时间与进度只读播放快照，不按墙钟走表；拖动时不写回，松手不发命令，下一次读快照回位；切歌、拔卡与停止态归零。_Why:_ 三态与进度的唯一真值是播放任务发布的快照，GUI 只发命令、读快照。_Source:_ [播放控制基线](../../Tools/gui_simulator/scenarios/music_transport.expected)、[ADR-0020](../adr/0020-playback-state-machine-ownership.md)、[播放后端公开合同](https://github.com/immorcoding/mp3_project/issues/53)、[音乐页呈现](https://github.com/immorcoding/mp3_project/issues/65)、[音乐页输入与列表](https://github.com/immorcoding/mp3_project/issues/66)
- **GUI-14** · provisional · 唱盘底图从 Pack 载入 SDRAM，由 vinyl 合成封面后绑定无事件 Image，图像尺寸与 view 一致；图标复用 LVGL 符号，图片不另编入内部 Flash。_Why:_ 区分资源加载、像素合成和输入，避免重复静态资源。_Source:_ [GUI 模块](../../Service/gui/README.md)、[资源 Service](../../Service/resource/README.md)
- **GUI-19** · provisional · 时间 Label 兼作状态行，不另建提示对象：停止且原因非 NONE 时常驻原因的英文短语，直到离开停止态或停止期间曲目身份变化，之后显示 `0:00/--:--`；已跳过坏文件数增加时临时显示 `Skipped k file(s)` 约 2 s，期间再增加则累加并重新计时，停止原因优先；停止次数增加时闪一次（约 300 ms）；两个计数以首次读快照为基线，只看差值；提示用单次 lv_timer、闪烁用 lv_anim，都按 LVGL tick 计时；三键与进度条不置灰，可用性只由停止原因表达。_Why:_ 合同只保证计数不漏、不保证逐次；固件只有 Montserrat 拉丁字形；按 LVGL tick 计时在模拟器中可重现；GUI 不另设可用状态源。_Source:_ [音乐页呈现](https://github.com/immorcoding/mp3_project/issues/65)、[播放后端公开合同](https://github.com/immorcoding/mp3_project/issues/53)、[GUI 侧 drain](https://github.com/immorcoding/mp3_project/issues/70)
- **GUI-20** · provisional · 唱盘当且仅当显示的三态为播放时旋转，暂停原地停、恢复从原角度续转；曲目身份变化或 position_ms 变小（同曲重选、停止归 0）时角度归零；角度由现有 Image 持有，同一 Image 至多一条旋转动画。_Why:_ 播放反馈与播放进度分离，输入只来自快照。_Source:_ [唱盘模块](../../Service/gui/main/vinyl/README.md)、[采用决定](https://github.com/immorcoding/mp3_project/issues/26)、[音乐页呈现](https://github.com/immorcoding/mp3_project/issues/65)

### Signals

- 2026-10-07 · cite · GUI-14 · 旋转沿用 vinyl 合成后的同一 Image 与 SDRAM 缓冲，不新增资源；角度 0 与原基线一致。
