# 资源加载 Service

本 Module 在 FreeRTOS 任务启动前完成当前产品 RPKC1 的一次性加载。它负责 NOR 物理位置、Vendor/Product 身份、必需资源表、业务 Metadata 约束、SDRAM 目标、复制后 CRC、Cache Clean、日志和致命失败诊断。

## 公开 Interface

- `Service_Resource_Init()` 映射 Flash、打开包并加载全部必需资源。
- `Service_Resource_IsReady()` 查询当前三项资源是否已经完整可用。
- `Service_Resource_GetDiagnostics()` 返回包身份及最近失败的 ResourceID、阶段和 Component 状态。

## 编译期依赖和运行路径

Service 包含 ResourcePack Component、Platform Flash、Log Component 和无状态 Cortex-M7 Cache Adapter 的公开 Interface。它不包含 W25Qxx、QSPI HAL 或链接器脚本实现，也不直接解释 RPKC1 字节字段。

运行路径为 APP → Resource Service → Platform Flash 映射与 ResourcePack 解析。复制目标是链接器预留的三个 SDRAM `NOLOAD` 区域；原有 FatFs CP936 查表和 LVGL 图片描述符仍引用这些地址，因此消费者不需要新增资源访问接口。

## 生命周期和限制

初始化成功后不向其他 Module 发布 NOR 指针或 Entry View。后续 FTL 间接操作可以退出并恢复 QSPI 映射，不会留下由任务长期持有的失效指针。当前三项资源全部为必需资源，任一缺失、类型或 Metadata 不匹配、CRC 错误、目标容量不足或 Cache Clean 失败都会阻止 APP 启动任务。

来源地址、容量、产品身份、资源 ID 和日志开关集中在 `resource_service_config.h`。完整链路见 [RPKC1 设计](../../docs/resource_pack_design.md)。设备侧从 SD 更新壁纸或模型时，须先经 FTL 暂存再写 Pack，见 [ADR-0015](../../docs/adr/0015-volume-roles-and-resource-install.md)；本 Module 当前仍只做启动一次性加载。
