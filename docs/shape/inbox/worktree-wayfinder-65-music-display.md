# Inbox: worktree-wayfinder-65-music-display

> 以下是 2026-10-09 [音乐页呈现：三态、进度、假 seek 回位、提示与唱盘](https://github.com/immorcoding/mp3_project/issues/65) 会话记下的 Signal，规则正文的改写尚未批准。完整结论见该票的决结评论，由音乐页播放交互 spec 承载。drain 时提给用户。

- 2026-10-09 · gui/transport · signal · GUI-10 · 时间 Label 显示“已解码/总时长”（m:ss/m:ss），duration_ms 为 0 时总时长写 `--:--`、进度条停在 0；进度 = position_ms / duration_ms。GUI-10 未写“总时长未知”的显示，drain 时补一句。
- 2026-10-09 · gui/transport · cite · GUI-10 · 假 seek 维持现状：Slider 为 PRESSED 时不写回，时间文字照旧显示真实已解码时间；松手不发命令，下一次读快照回位（暂停中、停止态与总时长未知时同样回位）。规则不用改，只把“最迟下一周期”改为“下一次读快照”（与 #53 的 Signal 合并处理）。
- 2026-10-09 · gui/transport · signal · GUI-10 · 时间 Label 兼作状态行：停止且原因不是 NONE 时常驻显示原因英文短语（如 `No SD card`），直到离开停止态；NONE 显示 `0:00/--:--`；已跳过坏文件数增加时临时显示 `Skipped k file(s)` 约 2 s，期间再增加则累加并重新计时，进入停止后停止原因优先；停止次数增加时状态行闪一次（约 300 ms，同一 Label 至多一条动画）。GUI-10 的“切歌、拔卡与停止态归零”要补上状态行，drain 时决定写进 GUI-10 还是另立规则。
- 2026-10-09 · gui/transport · signal · 无对应规则 · 字体只有 Montserrat（拉丁），所以提示文案只能用英文。中文提示要等资源包 FONT 解码，这项在本地图的 Out of scope 中。
- 2026-10-09 · gui/transport · signal · Proposed（唱盘旋转） · 用户采纳并改为读快照：旋转当且仅当显示的三态为播放；暂停原地停；曲目身份变化或 position_ms 变小（同曲重选、停止归 0）时角度归零。命令未回显时沿用上次采信的三态与曲目。drain 时可升为规则。
- 2026-10-09 · gui/transport · signal · 无对应规则 · 扫描中、拔卡、无曲目时三键与进度条都不置灰，按下后由停止原因提示；GUI 不另设“可用”状态源。
