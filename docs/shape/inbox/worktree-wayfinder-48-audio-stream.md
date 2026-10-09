# Inbox: worktree-wayfinder-48-audio-stream

> 以下是 2026-10-08 [音频流协议：开启、关闭、取消与流身份](https://github.com/immorcoding/mp3_project/issues/48) 会话记下的 Signal，规则正文的改写尚未批准。协议的完整结论见该票的解决评论，并由播放后端 spec 承载。drain 时提给用户。词典「音频流」词条已获用户当场批准，在本分支直接写入。

- 2026-10-08 · architecture/storage-seams · signal · ARC-12 · 规则写“当前唯一 Storage Task 执行，不预建跨任务队列”，出处是 ADR-0014 否决的通用文件请求队列。音频流协议新增了两个有真实消费者的跨任务交接：播放任务写的“想要的流”信箱，以及 Storage 写的流记录信箱，都按最新值覆盖。规则措辞需要说明，它禁止的是通用文件请求队列，而不是这类专用交接；FIL 仍只在 Storage Task 中使用。
- 2026-10-08 · storage/execution · signal · STOR-12 · 新约束：有打开的音频流时，Storage 不给 Flash 回收机会。停止态会关闭音频流，回收照常进行；暂停态保持流打开，回收顺延。STOR-12 的“上层提供有限回收机会”需要补上这道门。
- 2026-10-08 · storage/volumes · signal · STOR-8 · 消抖期间，除窗口请求外，Storage 也暂缓处理音频流请求和补块；已打开的流保持打开（消抖不等于卸载）。消抖结束后，卡仍在就继续处理，卡已拔则以 NOT_READY 或 CARD_REMOVED 收尾。规则里“暂停窗口请求”的范围要相应扩大。
- 2026-10-08 · storage · signal · Open questions · 开启音频流时由 Storage 越过 ID3v2 与尾部 ID3v1/APE，只送出音频区间，与词典「音频帧流」一致。“标签拟在填窗时按需读取”只涉及展示，0.6.0 不展示标签；这一条待决问题的措辞要和“越过标签归 Storage”分开。
