# Architecture Decision Records

本目录记录会长期约束 Module 所有权、Interface 接缝或演进路线的架构决定。ADR 不是实现进度记录；实现细节、调用链、配置参数和测试结果应留在对应技术文档与 Module README。

ADR 说明“为什么选择此方案、哪些替代方案未采用、改变后会影响什么”；技术文档说明“当前实际如何工作”。发生冲突时，不允许保留两份互相矛盾的事实：应在同一次修改中更新受影响的技术文档，并由最新已接受 ADR 记录该次取舍。

## 当前记录

- [0001-w25q256-component-seams.md](0001-w25q256-component-seams.md)：W25Q256、Flash FTL、Bridge、STM32 HAL Adapter 与 Platform 的职责划分。
- [0002-layering-and-interface-ownership.md](0002-layering-and-interface-ownership.md)：三类关系、Ops Interface 所有权与装配规则。
- [0003-pmic-softi2c-bridge.md](0003-pmic-softi2c-bridge.md)：AXP2101、SoftI2C、Bridge 与 Platform Power 的边界。
- [0004-log-single-consumer-and-output-adapter.md](0004-log-single-consumer-and-output-adapter.md)：日志单消费者、静态消息池与可替换输出后端。
- [0005-sd-filesystem-task-ownership.md](0005-sd-filesystem-task-ownership.md)：SD、FatFs、DMA 与 Storage Task 的所有权。
- [0006-cache-range-ownership.md](0006-cache-range-ownership.md)：Cortex-M7 D-Cache 范围与 DMA 缓冲区约束。
- [0007-gui-runtime-and-squareline-boundary.md](0007-gui-runtime-and-squareline-boundary.md)：GUI Task、GUI Service 与 SquareLine 生成边界。
- [0008-sdram-early-init-and-noload-ownership.md](0008-sdram-early-init-and-noload-ownership.md)：SDRAM 早期初始化、正式初始化与 NOLOAD 缓冲责任。
- [0009-w25q256-firmware-slots-and-diagnostic-reservation.md](0009-w25q256-firmware-slots-and-diagnostic-reservation.md)：W25Q256 的 OTA 固件双槽、自检区、Resource Pack 与 FTL 物理边界。

- [0010-fatfs-user-diskio-service-ownership.md](0010-fatfs-user-diskio-service-ownership.md)：USER DiskIO 自维护契约、弱默认/强接管及 Filesystem Service 的 Flash 执行所有权。
- [0011-ftl-copy-on-write-and-recovery.md](0011-ftl-copy-on-write-and-recovery.md)：FTL 整组异地提交、扫描恢复、显式格式化及首版 GC/磨损边界。
- [0012-filesystem-public-seams.md](0012-filesystem-public-seams.md)：Filesystem 公开接缝按卷/文件/诊断切开，`Maintain` 改名 `Reclaim`；其中 MSC 前瞻范围由 ADR-0013 取代。
- [0013-usb-msc-product-scope.md](0013-usb-msc-product-scope.md)：主线不支持 USB MSC，批量文件导入使用读卡器。
