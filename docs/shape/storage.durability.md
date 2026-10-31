# Storage · durability

FTL 持久化格式、可见提交和故障恢复。

[返回 Storage](storage.md)

### Rules

- **STOR-4** · settled · FTL 在异地数据与末尾提交页均验证后切换映射并确认单组完成；读取和恢复的最新已提交版本损坏报错，不静默回退；跨组请求及 FatFs 文件事务不承诺整体原子。_Why:_ 已确认数据不能被旧版本冒充，单组提交不能扩大为文件事务保证。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 持久化审阅、Fake NOR 撕裂写/CRC/跨组回归及真实掉电验收。
- **STOR-5** · settled · 恢复选最新可验证 epoch，最新 PREPARING 不被旧 READY 掩盖；双卷头按准备完成后才破坏数据、就绪完成后才确认的顺序更新；FTL/Service 挂载失败不自行格式化，显式恢复先安全收尾再扫描并作废旧文件对象。_Why:_ 恢复不能覆盖现状或重解释未完成格式化。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md)、[介质提交依据](sources/storage-format.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 持久化审阅及双卷头各阶段掉电/未格式化/恢复生命周期回归。
- **STOR-11** · settled · 格式逐字段显式编码并校验几何兼容；每次打开重建 RAM 映射，先按有效提交选择版本再验证获选整块；新块全块擦除须确认，单页每擦除周期只编程一次，跳过全擦除值页仍读回校验，局部更新保留同组其他扇区。_Why:_ 旧失效块残片、NOLOAD 和空元数据页都不能充当有效性证明。_Source:_ [FTL 格式](sources/storage-format.md)、[ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 持久化审阅及恢复分类/局部更新/跳页/兼容性回归。
