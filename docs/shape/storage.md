# Storage

曲库快照、卷生命周期、存储介质与持久化故障边界。分层、执行所有权、ISR 与文件删除通则沿用 [architecture](architecture.md) ARC-1、ARC-8–12。

Next id: STOR-6

## Pillars

- 曲库、播放列表和可见窗口分别表达事实、顺序和局部快照。
- 完成以数据持久化和硬件交还所有权为界，不以通知到达为界。
- 介质故障可诊断；恢复不得悄悄换回旧数据。

## catalog

SD 曲库与跨任务快照的语义。

### Rules

- **STOR-1** · settled · 曲库只收录 SD `Music/` 的相对路径；播放列表、游标与窗口带同一曲库代次，0 表示无效，GUI 只消费窗口副本；重扫或拔卡作废旧代次。_Why:_ 避免跨次扫描误用下标与跨任务整表共享。_Source:_ [ADR-0015](../adr/0015-volume-roles-and-resource-install.md)、[窗口契约](storage.catalog.md) _Check:_ storage catalog/listbuffer host 回归与存储语义审阅。

### References

- [曲库、窗口与元数据边界](storage.catalog.md)：改扫描、Queue 数据交接或游标时读。

## media

物理分区、DMA 及映射消费者的边界。

### Rules

- **STOR-2** · settled · Flash 诊断仅擦写首尾自检区，FTL 经 Bridge 校验分区内完整范围后访问；固件双槽与原始资源区独立保留，格式化和 GC 同样受边界约束。_Why:_ 逻辑盘维护不能破坏镜像或资源。_Source:_ [ADR-0009](../adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md)、[分区](storage.media.md#物理分区) _Check:_ Bridge/Platform 边界回归及分区审阅。
- **STOR-3** · settled · DMA 缓冲由可达且独占完整 Cache line 的内存承载；确认控制器停止访问并完成方向相关 Cache 收尾后才归还。FTL 整笔请求期间关闭 QSPI 映射，仅成功且硬件空闲后按入口状态恢复，失败保持关闭。_Why:_ 超时和 IRQ 不证明缓冲或映射已安全。_Source:_ [ADR-0006](../adr/0006-cache-range-ownership.md)、[介质参考](storage.media.md) _Check:_ embedded-reviewer 的 Cache/生命周期审阅与超时、延迟通知回归。

### References

- [SD 热插拔、NOR 协议与分区](storage.media.md)：改后端、原始诊断、DMA 或映射时读。
- [卷与执行接缝](storage.filesystem.md)：改挂载、通知或 FatFs 接缝时读。

## durability

FTL 提交与恢复的可见结果。

### Rules

- **STOR-4** · settled · FTL 在异地数据和末尾提交页均验证后切换映射并确认单组完成；最新已提交版本损坏报错，不静默回退。跨组请求与 FatFs 文件事务不承诺整体原子。_Why:_ 保留已确认数据语义，避免把旧数据伪装为恢复成功。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md) _Check:_ FTL Fake NOR 撕裂写/擦除、CRC、跨组失败回归及硬件掉电验收。
- **STOR-5** · settled · FTL 恢复选择最新可验证 epoch，最新 PREPARING 不被旧 READY 掩盖；FTL/Service 挂载失败不自行格式化，显式恢复先安全收尾再扫描并使旧文件对象失效。_Why:_ 不以恢复为名覆盖介质现状。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md)、[流程与 APP 策略](storage.filesystem.md) _Check:_ 双卷头掉电、未格式化/不兼容与恢复生命周期回归。

### References

- [提交、恢复、GC 与验证](storage.ftl.md)：改算法、同步语义或故障路径时读。
- [FTL v1 格式](storage.ftl-format.md)：改持久化编码、容量或制作介质解析工具时读。
