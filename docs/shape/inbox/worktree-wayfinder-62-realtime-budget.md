# Inbox: worktree-wayfinder-62-realtime-budget

> 以下是 2026-10-09 [实时预算：任务优先级、PCM 深度与最坏路径](https://github.com/immorcoding/mp3_project/issues/62) 会话记下的 Signal，规则正文的改写尚未批准。完整结论见该票的决结评论，由播放后端 spec 承载。drain 时提给用户。本票不改词典、不立 ADR。

- 2026-10-09 · gui · signal · 无对应规则 · GUI Task 是不让出的忙循环，只在等 LCD DMA 刷新时阻塞：Idle 几乎跑不到，运行时统计看不出 CPU 余量，Log/Monitor 只能靠时间片分到 CPU。播放任务的优先级高于它，所以对音频期限没有影响；要不要加帧间延时，留给 GUI 侧问题。
