# Storage · FTL v1 格式参考

[Storage](storage.md) 的持久化协议参考。此处数字是介质契约，改变需兼容性/迁移决定；恢复流程见 [FTL](storage.ftl.md)。

## 几何

扇区 512 B，七扇区为一个 3584 B 逻辑组；`group=LBA/7`，`offset=LBA%7`。每个版本占独立 4096 B 擦除块：头 `0..255`、数据 `256..3839`（14 个编程页）、末尾提交 `3840..4095`。

物理块数 N，数据块 D=N−2（扣双卷头），预留 R=ceil(D×预留百分比/100)，逻辑组 G=D−R，扇区 S=7G；运算防溢出并向上取整。分区容量与预留比例会改变卷几何，须与卷头比对，不能修改配置后重新解释已有盘；GC 水位只影响策略。现值由 Platform/FTL 私有配置维护。

## 编码

整数小端逐字段编码，不直接写 C struct。元数据页均 256 B，未用字节 `0xFF`，末尾 CRC@252..255 覆盖字节 0..251。CRC32 使用反射多项式 `0xEDB88320`、初值 `0xFFFFFFFF`、结果逐位取反；数据 CRC 各覆盖完整 512 B 扇区，不使用硬件 CRC 外设。

| 记录 | 字段偏移（字节） |
| --- | --- |
| 卷 PREPARING / READY | magic@0，format@4，epoch:u64@8，phase@16，数据物理块数@20，逻辑组数@24，扇区字节数@28，块字节数@32，组扇区数@36，CRC@252 |
| 组头 | magic@0，format@4，epoch:u64@8，逻辑组号@16，组版本:u64@20，七个扇区 CRC@28..55，页 CRC@252 |
| 提交 | magic@0，format@4，epoch:u64@8，逻辑组号@16，组版本:u64@20，头页 CRC@28，提交页 CRC@252 |

未标 u64 的字段均 u32。卷 magic=`0x314C4F56`、头=`0x31524746`、提交=`0x31544D43`；format=1，PREPARING=1，READY=2。两卷块各以页 0 写 PREPARING、页 1 写 READY。普通文件写入不更新卷头。

## 代次与拒绝条件

epoch 从 1 起，每次显式格式化递增；组版本从 1 起，0 无效，达到 `UINT64_MAX` 拒绝继续（INCOMPATIBLE）。同组同版本两个有效提交报 CORRUPT；有效提交选中的头或数据损坏同样报 CORRUPT。提交 CRC 无效按未提交处理，这是故障模型限制，不是任意损坏可恢复保证。

无任何有效卷描述报 UNFORMATTED，即使存在无法解析残片；最高 epoch 仅 PREPARING 报 INCOMPLETE；格式/几何不符报 INCOMPATIBLE。上述状态均不授权 FTL 自行格式化。
