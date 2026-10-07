# Architecture · storage-seams

文件系统与持久化层的公开接缝。

[返回 Architecture](architecture.md)

### Rules

- **ARC-11** · settled · FatFs USER 接缝由 FATFS Target 自维护契约/安全弱定义、生成区薄转发和 Service 强定义组成；Filesystem 是一个 Module，SD/Flash 私有分区，Service 持有唯一完成订阅与执行等待，Storage Task 决定时机；FTL 算法/GC/RawOps 属 Component，Platform 持有实例与映射生命周期。FatFs 不绕过 FTL 接原始 NOR；不预建 BlockDevice/Service/storage，USB MSC 不在产品范围。 _Why:_ 持久化算法、同步等待和任务策略需要各自唯一的拥有方。 _Source:_ [ADR-0010](../adr/0010-fatfs-user-diskio-service-ownership.md)、[ADR-0012](../adr/0012-filesystem-public-seams.md)、[ADR-0013](../adr/0013-usb-msc-product-scope.md) _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-12** · settled · APP 文件诊断只用 Filesystem 卷感知文件接口，不含 FatFs/逻辑块；Service 持有 FIL/DIR、路径/盘符转换和同步生命周期，APP 持有图样、计时、日志策略；当前唯一 Storage Task 执行，不预建跨任务队列。删除释放 FAT 簇，不按文件地址擦 NOR；无 TRIM 时仅 LBA 后续更新才使旧物理版本失效，GC 归 FTL。 _Why:_ 应用诊断不能绕开卷隔离和文件对象生命周期。 _Source:_ [ADR-0014](../adr/0014-filesystem-volume-aware-file-interface.md) _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
