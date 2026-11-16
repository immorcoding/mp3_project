# Storage

曲库快照、存储卷、物理介质与持久化结果的约束；分层和执行所有权沿用 [Architecture](architecture.md) ARC-11/12。

Next id: STOR-18

## Pillars

- 曲库是路径事实，播放列表是顺序，窗口是局部副本。
- 完成同时满足持久化承诺和硬件所有权交还。
- 恢复保留可诊断的介质现状，不用旧数据掩盖损坏。

## Open questions

- MP3 封面的展示：拟归 Now Playing，载荷待对应 spec 落定。播放时越过 ID3v2 与尾部 ID3v1/APE 归音频读取任务（见词典“音频帧流”），不属本问题。
- SD 读取超时后的 Abort：已定用有上界的显式 Abort，不加传输序号（[ADR-0024](../adr/0024-file-interface-concurrency.md)）；第 7 次板测 R2.2 用迟到 DMA 钩子 99 次连续 `victim=timeout verify=ok`，中止 44–45 µs，无内容 MISMATCH。`stuck`（中止停不住、`HAL_SD_DeInit` 后卷保持 ERROR）分支在健康卡上造不出，未测。
- Flash 运行中的 HAL 句柄恢复：任何 HAL 级 QSPI 超时都会让 `hqspi.State` 停在 ERROR，运行中没有恢复入口（`Platform_Flash_RecoverVolume` 走的 `HAL_QSPI_Command` 要求 READY），0.10.0 的做法是时限放大到 3000 ms、失败一次就停到重启；是否加句柄复位入口待评估，见 [Architecture · runtime](architecture.runtime.md) Proposed。

## Titles

- [catalog](storage.catalog.md): 曲目集合、顺序位置及跨任务可见快照。
- [volumes](storage.volumes.md): 可移除状态、卷就绪及文件对象失效。
- [media](storage.media.md): 物理分区、协议兼容与硬件访问所有权。
- [durability](storage.durability.md): FTL 持久化格式、可见提交和故障恢复。
- [execution](storage.execution.md): 回收推进、故障停机及完成判定。
