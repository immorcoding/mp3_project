# Inbox: worktree-wayfinder-50-dma-facts

> 以下是 2026-10-08 [DMA 完成事实、欠载与 Stop 失败语义](https://github.com/immorcoding/mp3_project/issues/50) 会话记下的 Signal 与草稿；规则正文的改写尚未批准。drain 时提给用户，与[播放状态机的唯一所有者与代码落点](https://github.com/immorcoding/mp3_project/issues/45)的 inbox 一起，在写两份 spec 之前处理。

- 2026-10-08 · architecture/ownership · signal · ARC-18 · 音频路径上已印证并细化：提交即 `Start`（两个半区一起提交）或 `SubmitHalf`，完成事件即 DMA 释放半区，交还发生在半区释放后或 `Stop` 返回 OK 时。规则目前只写“完成后交还”，没有覆盖两种情况：Stop 成功提前交还；锁存 `STOP_FAILED` 时缓冲永久留在 Platform、直到重启。drain 时考虑在正文补上“或停止成功后”与故障例外。
- 2026-10-08 · architecture/interrupts · signal · ARC-10 · 规则约束了欠载处理：ISR 只累加计数并唤醒任务，不写 XSMT 静音脚，也不调 Stop。欠载静音因此改走任务上下文的 Stop/Start，旧 #33“ISR 侧静音续播”的思路被排除。规则照常执行，无需改写。
- 2026-10-08 · hardware · draft · 候选硬件事实（数值为推断，待板测）：SPI123 内核时钟取自 PLL3，I2S2（音频）与 SPI1（LCD）共用；0.6.0 不改 PLL3，只靠 I2S 分频切换采样率，推算 48 k 约 47.86 kHz、44.1 k 约 44.27 kHz、32 k 约 32.00 kHz。板测确认后再考虑写入 `sources/hardware-facts.md`。
