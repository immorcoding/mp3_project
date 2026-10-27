# GUI · music

音乐页：窗口交接与播放列表行的稳定呈现，播放控制与唱盘呈现。

[返回 GUI](gui.md)

### Rules

- **GUI-9** · provisional · Queue 只消费 READY 窗口副本，行数按 Length 且不超过容量，高亮须与当前 Catalog 代次一致；GUI 不解析路径或依赖 storage_listbuffer.h。 _Why:_ 窗口是跨任务快照，不能泄漏整表或混用旧代次。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
- **GUI-16** · provisional · Queue 换窗回收 panel 并保留余像素，非首窗保留回滑行。 _Why:_ 换窗不能改变手指对应的像素位置或阻断反向滚动。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
- **GUI-17** · provisional · Queue 点行先由 Service 即时高亮本窗再 post 下标，GUI 发 SELECT{已显示窗口代次, 位置}；GUI 给发送成功的命令编递增序号，快照回显序号落后时曲目与三态冻结在最后一份已追上的快照、位置照用；命令队列满则丢弃本次输入、记日志，并把即时高亮撤回到快照所示；当前曲高亮须回显已追上、快照代次非 0 且等于窗口代次；Queue 不随当前曲自动滚动。 _Why:_ 异步回显不能把刚选中的行或刚按下的状态覆盖回旧值，GUI 也不预测后端。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[播放后端公开合同](https://github.com/immorcoding/mp3_project/issues/53)、[音乐页输入与列表](https://github.com/immorcoding/mp3_project/issues/66)
- **GUI-18** · provisional · Queue 行保持边宽和状态占位固定，只切换 Opa、调色板角色和 Long mode；封面归 Now Playing。 _Why:_ 状态变化不应挪动文字，列表行与播放封面各有展示职责。 _Source:_ [Queue 基线](../../Tools/gui_simulator/scenarios/queue.expected)、[GUI 任务](../../APP/tasks/gui/README.md)
- **GUI-10** · provisional · 时间、进度与三键仅在 Now Playing；三键发 PREV/TOGGLE/NEXT，连按不合并，GUI 不持 playing 与游标，三态与曲目按 GUI-17 采信；时间 Label 显示“已解码/总时长”，总时长未知写 `--:--`、进度条停 0；时间与进度只读播放快照，不按墙钟走表；拖动时不写回，松手不发命令，下一次读快照回位；切歌、拔卡与停止态归零。_Why:_ 三态与进度的唯一真值是播放任务发布的快照，GUI 只发命令、读快照。_Source:_ [播放控制基线](../../Tools/gui_simulator/scenarios/music_transport.expected)、[ADR-0020](../adr/0020-playback-state-machine-ownership.md)、[播放后端公开合同](https://github.com/immorcoding/mp3_project/issues/53)、[音乐页呈现](https://github.com/immorcoding/mp3_project/issues/65)、[音乐页输入与列表](https://github.com/immorcoding/mp3_project/issues/66)
- **GUI-14** · provisional · 唱盘底图从 Pack 载入 SDRAM，由 vinyl 合成封面后绑定无事件 Image，图像尺寸与 view 一致；图标复用 LVGL 符号，图片不另编入内部 Flash。_Why:_ 区分资源加载、像素合成和输入，避免重复静态资源。_Source:_ [GUI 模块](../../Service/gui/README.md)、[资源 Service](../../Service/resource/README.md)
- **GUI-19** · provisional · 时间 Label 兼作状态行，不另建提示对象：停止且原因非 NONE 时常驻原因的英文短语，直到离开停止态或停止期间曲目身份变化，之后显示 `0:00/--:--`；已跳过坏文件数增加时临时显示 `Skipped k file(s)` 约 2 s，期间再增加则累加并重新计时，停止原因优先；停止次数增加时闪一次（约 300 ms）；两个计数以首次读快照为基线，只看差值；提示用单次 lv_timer、闪烁用 lv_anim，都按 LVGL tick 计时；三键与进度条不置灰，可用性只由停止原因表达。_Why:_ 合同只保证计数不漏、不保证逐次；固件只有 Montserrat 拉丁字形；按 LVGL tick 计时在模拟器中可重现；GUI 不另设可用状态源。_Source:_ [音乐页呈现](https://github.com/immorcoding/mp3_project/issues/65)、[播放后端公开合同](https://github.com/immorcoding/mp3_project/issues/53)、[GUI 侧 drain](https://github.com/immorcoding/mp3_project/issues/70)
- **GUI-20** · provisional · 唱盘当且仅当显示的三态为播放时旋转，暂停原地停、恢复从原角度续转；曲目身份变化或 position_ms 变小（同曲重选、停止归 0）时角度归零；角度由现有 Image 持有，同一 Image 至多一条旋转动画。_Why:_ 播放反馈与播放进度分离，输入只来自快照。_Source:_ [唱盘模块](../../Service/gui/main/vinyl/README.md)、[采用决定](https://github.com/immorcoding/mp3_project/issues/26)、[音乐页呈现](https://github.com/immorcoding/mp3_project/issues/65)
- **GUI-21** · exploring · Now Playing 在唱盘与进度条之间显示当前曲目显示名：12 px 居中，可见宽 160 px 单行，超宽循环滚动，不换行、不截断、不加省略号；曲名区只显示名字、空白（已采信新身份、应答未到）或 `--`（无当前曲，或防御性取不到），不保留旧名、不显示加载中；按快照归约已追上的曲目身份请求，以请求时的身份采信应答，身份已变即丢弃并重新请求。_Why:_ 切歌与连按时名字须与当前曲一致，异步应答不能把旧名写回。_Source:_ [唱盘页曲名的呈现](https://github.com/immorcoding/mp3_project/issues/103)、[曲目显示名的数据路径与所有权](https://github.com/immorcoding/mp3_project/issues/104)、[0.7.0 spec](https://github.com/immorcoding/mp3_project/issues/112)
- **GUI-22** · exploring · 只有曲名 Label 用资源包 FONT 解码出的 Noto 12 px 字体，ASCII 同源；字体不可用时该 Label 回退 Montserrat 12；缺字画占位框，整名不改 `--`；其他 Label 仍用 Montserrat。_Why:_ 中英混排的行高与基线须一致，字体缺失不能让播放器不可用。_Source:_ [CJK 字体方案](https://github.com/immorcoding/mp3_project/issues/105)、[0.7.0 spec](https://github.com/immorcoding/mp3_project/issues/112)

### Signals

- 2026-10-18 · cite · GUI-17 · 归约器按 C1 冻结整份快照：身份、三态、停止原因、总时长取最后一份已追上的快照，位置与两个计数取当前份；C6 的“进入停止”以冻结快照状态加当前 `StopCount` 判定，原因不变的再次停止（AUDIO_FAULT 锁存后再按播放）也重新显示短语（同时引 GUI-10）。
- 2026-10-07 · cite · GUI-14 · 旋转沿用 vinyl 合成后的同一 Image 与 SDRAM 缓冲，不新增资源；角度 0 与原基线一致。
- 2026-10-19 · cite · GUI-19 · 优先级“常驻文本 > 跳过提示 > 时间”只在 `Service/gui/main/transport` 内；跳过提示为单次 `lv_timer`（2000 ms，累加时 `lv_timer_reset`），闪烁为以时间 Label 为 var 的 `lv_anim`，重复调用先删旧动画；模拟器日志 `music_stop_reasons` 连按两次播放后动画数只比静止时多一条。
- 2026-10-26 · cite · GUI-21 · “以请求时的身份采信应答”在冷启动有一个按 #104 定下的例外：同一位置代次 0 → G 时，已显示名字或在途应答的代次等于 G 视为同一首，不清空、不重请求；其余身份变化一律清空并重请求。实现在 `gui_music_title_poll()` 内，调用方不变（#120）。
