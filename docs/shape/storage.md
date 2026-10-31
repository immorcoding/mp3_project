# Storage

曲库快照、存储卷、物理介质与持久化结果的约束；分层和执行所有权沿用 [Architecture](architecture.md) ARC-11/12。

Next id: STOR-18

## Pillars

- 曲库是路径事实，播放列表是顺序，窗口是局部副本。
- 完成同时满足持久化承诺和硬件所有权交还。
- 恢复保留可诊断的介质现状，不用旧数据掩盖损坏。

## Open questions

- MP3 封面的展示：拟归 Now Playing，载荷待对应 spec 落定。播放时越过 ID3v2 与尾部 ID3v1/APE 归 Storage（见词典“音频帧流”），不属本问题。
- SD 读取超过 `FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS`（500 ms，按每个 32 KiB DMA 分块）后，迟到的 SDMMC DMA 完成事件是否会被下一次传输误当作完成：超时只把设备置 ERROR 并不中止在途 DMA，下次传输前会清通知挂起位，残余窗口是迟到 IRQ 落在恢复之后、清位之后，而通知值不带传输序号。主机测试无法证实，待上板构造超时（拔卡或低速卡）后紧接读写；成立则给通知加传输序号，或在恢复路径显式 Abort DMA（hw:pending，见板上状态）。

## Titles

- [catalog](storage.catalog.md): 曲目集合、顺序位置及跨任务可见快照。
- [volumes](storage.volumes.md): 可移除状态、卷就绪及文件对象失效。
- [media](storage.media.md): 物理分区、协议兼容与硬件访问所有权。
- [durability](storage.durability.md): FTL 持久化格式、可见提交和故障恢复。
- [execution](storage.execution.md): 回收推进、故障停机及完成判定。
