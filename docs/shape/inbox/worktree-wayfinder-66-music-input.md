# Inbox: worktree-wayfinder-66-music-input

> 以下是 2026-10-09 [音乐页输入与列表：命令映射、高亮、任务节奏与模拟器后端](https://github.com/immorcoding/mp3_project/issues/66) 会话记下的 Signal，规则正文的改写尚未批准。完整结论见该票的决结评论，由音乐页播放交互 spec 承载。drain 时提给用户。SELECT 改为“选中并播放”属于公开合同改动，已写进该票的“留给其他票”，本分支不改合同、ADR 与词典。

- 2026-10-09 · gui/transport · signal · GUI-10 · 输入到命令：Now Playing 三键分别发 PREV / TOGGLE / NEXT，进度条松手（SEEK 输入）不发命令（假 seek，回位呈现归 [#65](https://github.com/immorcoding/mp3_project/issues/65)）；`gui_music.c` 不再读写游标、不再持本地 playing，transport 结构体只留假 seek 拖动所需。连按 N 次就是 N 条命令，GUI 不合并。板上没有用户实体按键（AXP2101 电源键不在本范围），“按键”只指屏幕按钮。
- 2026-10-09 · gui/queue · signal · GUI-17 · 改写方向：Queue 点行仍由 Service 先在本窗即时高亮再 post 下标，GUI Task 发 SELECT{已显示窗口代次, 位置}；回显序号落后时，曲目与三态冻结在最后一份已追上的快照（不预测），位置照用快照；命令队列满时丢弃本次输入、记日志、把本窗即时高亮撤回到快照所示，不提示。
- 2026-10-09 · storage · signal · STOR-6 · 高亮条件补充“回显已追上”：回显已追上、快照身份代次非 0 且等于已显示窗口代次时才按快照位置高亮，否则不高亮（未追上时保留本窗即时高亮）。Queue 不自动滚动跟随当前曲。GUI 不再包含 `storage_playback_cursor.h`。
- 2026-10-09 · gui/regression · signal · GUI-4 · 模拟器的替身边界扩到 APP 音乐分区：编译真实的 `APP/tasks/gui/music`，删掉 `sim_main.c` 演示分区，新增实现播放合同头的假后端（按命令响应、按虚拟时钟推进位置，按键注入拔卡、坏文件，并能挂起/恢复命令处理）和假 Storage 窗口。基线分两步：先等价替换，旧场景哈希不变，变化的帧逐张看图、写明原因；再接新行为并新增场景（切歌、暂停中上一首/下一首保持暂停、暂停中点行开播、坏文件跳过与 ALL_BAD、拔卡、未追上冻结、队列满撤回）。涉及提示的帧依赖 #65 的呈现。
- 2026-10-09 · gui · signal · 无对应规则 · GUI Task 忙循环维持不变，不加帧间让出或限帧（对音频期限无影响，CPU 占比按播放任务自身份额测，见 [#62](https://github.com/immorcoding/mp3_project/issues/62)）；GUI 最长一帧只记录，不设体验阈值。drain 时决定是否需要写成规则，或只留在交互 spec。
