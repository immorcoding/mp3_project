# ADR-0012：Filesystem 公开接缝切开与 MSC 首版范围

- 状态：已接受、已实施
- 日期：2026-09-01
- 技术方案：[Filesystem Service 公开接缝切开](../filesystem_service_reshape.md)
- 相关决定：[ADR-0005](0005-sd-filesystem-task-ownership.md)、[ADR-0010](0010-fatfs-user-diskio-service-ownership.md)

## 背景

Filesystem Module 在接入 Flash FTL、文件槽和启动诊断后，公开头同时暴露卷生命周期、文件访问和原始 NOR 诊断。APP 还保留一层无逻辑的 `storage_flash` 转发。USB MSC 即将接入，但第二个块消费者尚未出现。

## 决定

Filesystem 仍是一个 Module，不新建 `Service/storage`，不抽通用 BlockDevice，不预建 SD 所有权层。公开 Interface 按调用者分成卷、文件、诊断三个头。MSC 首版只导出 SD；所有权门槛和跨任务命令在 MSC 实现时加在现有 `BSP_SD_* → filesystem_sd_transfer_*` 之间。Flash 不进入 MSC。启动诊断由 APP benchmark 直接调用诊断头，删除 `storage_flash` 转发。自维护链路上的 `Maintain` 公开符号改为 `Reclaim`，表示一次有限 GC 机会。

## 不采用的方案与后果

- 现在抽出 `Service/storage` 或通用块设备，会在只有一个块消费者时得到浅转发。
- 文件级卷选择对 MSC 无用；MSC 要的是逻辑块与每卷所有权。
- 为诊断复制 Platform 区域枚举没有语义增益。
- 仅为未来 MSC 预建 `filesystem_sd_access` 同样是单 Adapter 假接缝。
