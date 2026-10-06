# Flash FTL Component

本 Module 已实现 NOR 到 512 B 逻辑扇区的转换：七扇区组映射、异地提交、启动扫描、循环分配、整块 GC 和故障安全收尾。主机回归通过，真实掉电验收待完成；详细格式、参数与测试边界见 [设计文档](../../docs/shape/storage.ftl.md)。

## 公开 Interface

- `FlashFTL_Init()` 注入长期 RawOps、Context 和内存，不访问数据阵列。
- `OpenStart/FormatStart/ReadStart/WriteStart/SyncStart/ReclaimStart` 受理操作；返回 OK 只表示受理。
- `FlashFTL_Process()` 返回 RUNNING（软件可继续）、WAIT（硬件或安全收尾待完成）或最终结果。
- `FlashFTL_Abort()` 请求失败收尾，之后仍须推进 Process；不承诺回滚。
- `GetInfo/GetDiagnostics/IsReady` 提供容量、诊断与就绪查询。
- `FlashFTL_RawOpsTypeDef` 由本 Component 拥有，表达几何、原始读/编程/擦除、推进和 Quiesce。

## 编译期依赖与运行路径

只依赖 ISO C 和本 Module，不包含 W25Qxx、HAL、RTOS、Platform、FatFs 或业务头。运行时由 Platform 调用 FTL，FTL 调用注入的 RawOps，Bridge 再调用 W25Qxx；这不构成 FTL 对 W25Qxx 的编译依赖。

## 生命周期与内存

Platform 长期持有 Handle、映射表、版本表、块状态、4096 B Work 与 512 B Scratch。Component 不申请堆内存，不绑定 MCU 地址或链接段。每次打开重建表，不信任 SDRAM 残留。单个普通执行上下文、同时一笔操作；输入缓冲直到操作完成或安全收尾结束前不可改写，禁止在 ISR 调用。

## 存储与故障约束

新块数据验证、最后写入提交页并验证后才切换映射；无 RAM 写回早确认。未映射读返回 0xFF，跳过全 0xFF 数据页编程仍须回读验证。启动先以有效提交页选择最新版本，再校验获选整块，允许旧失效块被 GC 擦到一半，但不能静默回退损坏的最新记录。

GC 只擦失效块，一次回收最多一块；前台不足先回收。策略宏在私有 `flash_ftl_config.h`，不是其他 Module 的包含接口。无自动格式化、静态磨损均衡、持久化游标或完整映射检查点；跨组请求和 FatFs 文件事务不保证整体原子。Quiesce 未确认控制器/DMA 停止访问前始终保持操作所有权。
