# ADR-0005：SD、FatFs 与 Storage Task 的所有权

- 状态：已接受（按现有代码追溯记录）
- 日期：2026-08-29
- 相关实现说明：[../sd_architecture.md](../sd_architecture.md)

## 背景

SD 卡既有热插拔，也有 SDMMC DMA、D-Cache 和 FatFs 同步 DiskIO 契约。直接让 APP、FatFs 或未来业务分别访问 HAL SDMMC，会重复设备生命周期和并发规则。

## 决定

1. `Components/sd` 是面向逻辑块的 SD Card Device，持有生命周期、范围校验、DMA `BUSY` 推进和归一化错误；它不认识 SDMMC、FatFs 或 FreeRTOS。
2. `Platform/sd` 装配唯一卡槽、卡检测回调和 SDMMC IRQ 节点；它不持有任务通知索引、FatFs 或挂载策略。
3. Storage Task 是当前 SD 热插拔、卷挂载/卸载与本地 FatFs 的唯一普通任务上下文：索引 0 用于消抖，Filesystem Service 私有 DMA 执行器在同一任务中使用索引 1。
4. `Service/filesystem` 通过 `BSP_SD_*` Override Seam 实现 FatFs 的同步块访问，并用专用 AXI SRAM bounce buffer 隐藏 DMA 对齐和 Cache 规则。
5. 当前不创建通用 `BlockDevice` 或 `Service/storage`；只有 W25Qxx 的 FTL 完成后出现第二个真实逻辑块实现，才从实际共同需求中提取最小 Interface。

## 后果

- 未插卡是正常持续状态，不阻止系统启动；
- DMA 完成与 FatFs 调用均在任务上下文收尾，ISR 只发布事件；
- 当前产品不提供 USB MSC；本地 FatFs 是 SD 介质的唯一文件系统所有者。
