# Flash FTL Component

本 Module 预留给 NOR Flash Translation Layer（FTL）。它的职责是把原始 NOR Flash 的页编程、擦除块和就绪规则转换为上层可安全消费的逻辑扇区；当前仅建立目录和架构约束，尚未创建源代码或确定具体映射算法。

## 预期公开 Interface

- `FlashFTL_*`：未来的逻辑扇区访问、同步和诊断 Interface；
- 由本 Component 拥有的 `FlashFTL_RawOps`：表达 FTL 实现所需的原始 Flash 读取、编程、擦除、就绪等待与几何信息。

## 编译期依赖

- ISO C 与本 Module 的私有元数据格式；
- 注入的 `FlashFTL_RawOps` 与配套 Context。

本 Component 不包含 `Components/w25qxx`、Adapter、Platform、HAL、FatFs、USB MSC、FreeRTOS 或媒体业务头文件。

## 运行时请求路径

FTL 通过自身拥有的 `FlashFTL_RawOps` 请求原始 Flash 操作；`Adapters/bridge/flash_ftl_w25qxx` 将来把该 Ops 转换为 W25Qxx Component 的公开 Interface。上层访问逻辑扇区时不需要知道页大小、擦除块或芯片指令。

## 生命周期与约束

- `Platform/flash` 长期持有 FTL Handle，并在 W25Qxx Device 完成装配后绑定 Raw Ops；
- 擦写映射、备用块、同步语义和掉电一致性属于本 Component，不能泄漏到 FatFs 或 Platform；
- 未完成明确的擦除、对齐、写入和掉电策略前，不得把原始 W25Qxx 直接伪装成 FatFs 逻辑盘。
