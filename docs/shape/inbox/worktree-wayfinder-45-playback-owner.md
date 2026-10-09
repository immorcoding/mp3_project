# Inbox: worktree-wayfinder-45-playback-owner

> 以下是 2026-10-08 [播放状态机的唯一所有者与代码落点](https://github.com/immorcoding/mp3_project/issues/45) 会话记下的 Signal，规则正文的改写尚未批准。drain 时提给用户，按[音乐页播放交互 spec](https://github.com/immorcoding/mp3_project/issues/44) 的结论决定怎么改。ADR-0020 与词典不属于 area 文件，已在本分支直接写入。

- 2026-10-08 · storage · signal · STOR-6 · [ADR-0020](../../adr/0020-playback-state-machine-ownership.md) 规定游标只由播放任务写、GUI 的当前曲以播放状态为准，Storage 重建时不再重置游标；“高亮同时核对游标与窗口代次”的依据要改为播放状态里的序号，代次如何对应留给公开合同决定。
- 2026-10-08 · gui/queue · signal · GUI-17 · 按 ADR-0020，GUI 点击 Queue 行后不再更新游标，而是向播放任务发送选曲命令；“cursor 更新后才 Apply”的顺序要改写为“以播放状态为准”，防止旧状态覆盖刚选中行的保护仍然需要。
- 2026-10-08 · gui/transport · signal · GUI-10 引用的“APP 既有 playing 与游标事件”将改为由播放状态驱动；GUI 不再持有 playing 真值（ADR-0020 第 6 条）。
