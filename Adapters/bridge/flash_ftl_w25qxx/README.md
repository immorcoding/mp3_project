# Flash FTL W25Qxx Bridge

已实现 `FlashFTL_W25QxxBridge_Bind()`，将 W25Qxx 的原始 NOR 操作接入 FTL 拥有的 RawOps。编译与主机组件回归已完成，实际板级边界/故障验收见 [FTL 设计](../../../docs/shape/storage.md#durability)。

## 所有权与依赖

只包含 FTL、W25Qxx 的公开 Interface；不包含 HAL、CMSIS、CubeMX、FreeRTOS、FatFs、Platform、Service 或 APP。Bridge 不拥有芯片或 FTL 实例，不分配内存；Platform 持有两个实例与 Bridge Context，并注入分区和长期内存。

## 运行路径与约束

FTL RawOps → Bridge → W25Qxx 公开读、单页编程、擦除、Process、Quiesce。Bind 要求已识别的 W25Q256、可用 Quiesce、4 KiB 对齐且不越芯片的完整分区。每笔操作检查分区相对范围、长度及页/擦除边界后，转换为绝对地址。

Bridge 转换返回状态，不推进 GC，不等待任务，不直接控制 DMA。Quiesce 成功仅说明控制器不再访问缓冲；NOR 内部操作是否完成由后续显式恢复确认，不能解释为擦写已取消。
