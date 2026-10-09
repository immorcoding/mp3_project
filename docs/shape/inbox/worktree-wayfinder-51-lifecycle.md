# Inbox: worktree-wayfinder-51-lifecycle

> 以下是 2026-10-08 [生命周期边界：EOF 排空、暂停换曲与短文件](https://github.com/immorcoding/mp3_project/issues/51) 会话记下的 Signal；规则正文的改写尚未批准。drain 时提给用户，与 #45、#48、#50 的 inbox 一起，在写两份 spec 之前处理。

- 2026-10-08 · gui/transport · cite · GUI-10 · “切歌归零”得到印证并更细：播放任务接受切歌命令（含播完后自动前进）的同一刻，曲目即为新游标、位置为 0；总时长在首帧解出 Xing/VBRI 或估算之前未知。GUI-10 没有说明“总时长未知”时如何显示，交给地图 GUI 侧问题与[播放后端公开合同](https://github.com/immorcoding/mp3_project/issues/53)，drain 时看是否要在 GUI-10 补一句。
- 2026-10-08 · gui/transport · cite · GUI-10 · 暂停中进度冻结在已解码时间；同曲恢复会重放暂停时正在读的半区，但进度不回退。与“进度来自解码器时间”一致，规则不用改。
- 2026-10-08 · architecture/ownership · cite · ARC-18 · 生命周期各转换点都在 Stop 返回 OK 之后才重写半区：切歌时先写请求信箱再 Stop，Stop 成功后丢弃半区内容；暂停中换曲时缓冲已交还，可以直接预填。印证了 #50 inbox 里“Stop 成功即交还”的补充建议，没有新的冲突。
