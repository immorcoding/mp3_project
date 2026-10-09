# Inbox: worktree-wayfinder-49-block-pool

> 以下是 2026-10-08 [块池与跨块解码输入](https://github.com/immorcoding/mp3_project/issues/49) 会话记下的 Signal，规则正文的改写尚未批准。完整结论见该票的解决评论，由播放后端 spec 承载。drain 时提给用户。词典新增的“音频块”“断流”两条已获用户当场批准，直接写在本分支。

- 2026-10-08 · storage · confirm · STOR-3 · “FatFs 任意缓冲经专用 bounce buffer 转交”直接决定了音频路径不承诺零拷贝：SD DMA → bounce buffer → memcpy 进音频块 → memcpy 进播放模块的线性窗口。音频块只由 CPU 访问，不作为 DMA 目标。如果将来让 SDMMC 直接写块，需要另行决定，并改写本规则。
- 2026-10-08 · architecture/ownership · signal · ARC-18 · 音频块不是 DMA 缓冲，所有权不按传输时段转移，而是“谁持有指针谁拥有”：在空闲队列中归 Storage，填好入就绪队列后交给播放任务，归还后回到 Storage。ARC-18 只管 DMA 缓冲，不覆盖这类经队列转移的 CPU 缓冲。复核 ARC-18 时（地图上的“Shape/ADR 迁移”雾区），需要决定这个模式是否另立规则，或者在 ARC-18 中写明范围。
- 2026-10-08 · hardware/sdram · signal · HWD-2 · 新增 SDRAM NOLOAD 段：音频块池初值 32 × 32 KiB = 1 MiB。块内容只在 Storage 填好后入队时才有效，不依赖清零。按 HWD-2 审查启动顺序、MPU 与全容量诊断的时机；块池不作 DMA 目标，不涉及 Cache/DMA 所有权。
- 2026-10-08 · hardware/placement · confirm · HWD-6 · 播放模块的线性输入窗口（初值 4 KiB，下限 2308 B）只由 CPU 访问，放 DTCM，属于规则中“读缓冲”一类，与规则一致。
