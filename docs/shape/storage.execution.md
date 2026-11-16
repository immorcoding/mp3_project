# Storage · execution

回收推进、故障停机及完成判定。

[返回 Storage](storage.md)

### Rules

- **STOR-12** · provisional · GC 只擦已证明不承载当前数据的块；已擦除空闲数按全池预留预算和滞回水位管理，分配保持保护下限，无可回收块即结束；上层提供有限回收机会，当前一次最多一块，不承诺擦除抢占或静态磨损均衡；打开的音频流不影响回收机会，前提是每卷一把锁，且运行中没有任务读 QSPI 映射窗口，新增这类读者要重新评估。_Why:_ 回收是有限进展而非必然释放空间，压力不能成为擦当前版本的理由；回收和读块分到两个任务、SD 与 Flash 各一把锁后，擦除不再挡补块。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md)、[FTL 接口](../../Components/flash_ftl/README.md)、[后台任务的作业模型](https://github.com/immorcoding/mp3_project/issues/196)
- **STOR-13** · settled · 执行器区分软件可继续与等待硬件，FTL 内部原始操作只由 FTL 推进；错误后停接新请求，校验失败保留旧映射不自动换块重试，Quiesce 未确认前保持缓冲所有权。Sync 确认先前写入与硬件安全而非清空 GC；失败返回不能解释为介质未变化。_Why:_ 软件步骤未必有 IRQ，Abort 不撤销 NOR 写擦，提交后通知也可能丢失。_Source:_ [FTL 接口](../../Components/flash_ftl/README.md)、[故障模型](sources/storage-format.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的规范轴核查推进/安全收尾，覆盖通知丢失、中止失败与介质仍忙。

### Signals

- 2026-11-16 · cite · STOR-12 · 播放中真擦除是新设计，用户 2026-11-04 决定从 settled 降为 provisional；第 7 次板测（#221）R6.2 通过（30 min 擦除 255 → 9692 次，无回收失败，欠载 0；回收是后台任务的作业，100 ms 周期、一次一块，播放和暂停时都提供机会），但 R6.3 ①（Flash 删旧建新那一刻表锁 20.22 ms，超线一次，下版 bug 票 #228）和 R6.4（耗尽时写 32 KiB 最长 1.27 s，满卷只写到 9.0 MiB，没压到 16 MiB）判“部分”，所以暂不升回。升回时先核对前提“运行中没有任务读 QSPI 映射窗口”仍成立，并恢复 _Check:_（[CODING_STANDARDS](../../CODING_STANDARDS.md) 回收审阅及水位/无失效块/持续覆盖回归）。
- 2026-11-09 · friction · STOR-12 · 第 6 次板测：回收与写入在优先级 1 的后台任务里被抢占，`W25Qxx_Process` 把已完成的页编程判成超时、HAL 的 QSPI 100 ms 时限把 `hqspi.State` 置成 ERROR；运行中没有恢复 HAL 句柄的入口，Flash 到重启都不可用，回收“失败一次就停，到 Flash 卷序号变化为止”。已修：先读墙钟再读完成状态，`PLATFORM_FLASH_QSPI_TIMEOUT_MS` 3000 ms，回收与恢复预算 5000 ms（见 [Architecture · runtime](architecture.runtime.md) Proposed）；运行中的 HAL 句柄恢复是否要做见 [Storage](storage.md) 未决问题。
