# 资源加载 Service

本 Module 在 FreeRTOS 任务启动前完成当前产品 RPKC1 的一次性加载。它负责 NOR 物理位置、Vendor/Product 身份、资源表（四项必需 + 一项可选）、业务 Metadata 约束、SDRAM 目标、复制后 CRC、Cache Clean、日志和必需资源的致命失败诊断。

## 公开 Interface

- `Service_Resource_Init()` 映射 Flash、打开包，加载四项必需资源与可选字体。
- `Service_Resource_IsReady()` 查询四项必需资源是否已经完整可用；不反映可选字体。
- `Service_Resource_GetDiagnostics()` 返回包身份及最近失败的 ResourceID、阶段和 Component 状态；只记录包级与必需资源的失败。
- `Service_Resource_GetFont()` 只读查询可选字体载荷：是否可用，以及它在 Resource 自有 SDRAM 区中的起点与长度。不发布 NOR 指针或 Entry View；结果在 Init 之后不再变化。

## 资源表

| ResourceID | 内容 | 类型 | 必需 | SDRAM 目标 |
|---|---|---|---|---|
| 1 / 2 | CP936 Unicode↔OEM 表 | BINARY | 是 | `__external_resource_uni2oem_*` / `__external_resource_oem2uni_*` |
| 3 | 默认壁纸 | IMAGE | 是 | `__external_resource_wallpaper_*` |
| 4 | 唱盘底图 | IMAGE | 是 | `__external_resource_vinyl_*` |
| 5 | 曲目元数据 CJK 字体（`lv_font_conv` bin） | FONT | 否 | `__external_resource_font_*`（`.sdram_font_resource`，0xA4800 B） |

可选项排在最后加载。FONT 的业务约束是 `ResourcePack_DecodeFontMetadata()` 通过（格式、编码、bin 分段与 Metadata 一致、位图小于 1 MiB）且标称像素高度为 12。

## 编译期依赖和运行路径

Service 包含 ResourcePack Component、Platform Flash、Log Component 和无状态 Cortex-M7 Cache、周期计数器 Adapter 的公开 Interface。它不包含 W25Qxx、QSPI HAL 或链接器脚本实现，也不直接解释 RPKC1 字节字段。

运行路径为 APP → Resource Service → Platform Flash 映射与 ResourcePack 解析。复制目标是链接器预留的五个 SDRAM `NOLOAD` 区域；原有 FatFs CP936 查表和 LVGL 壁纸描述符仍引用这些地址，唱盘底图由 `main/vinyl/` 按同一槽位构造描述符。字体是唯一经查询接口发布的资源：`Service/gui` 的字体子模块在 GUI Task 中调用 `Service_Resource_GetFont()`，只读 FONT 区并解码到自己的区。

## 生命周期和限制

初始化成功后不向其他 Module 发布 NOR 指针或 Entry View。后续 FTL 间接操作可以退出并恢复 QSPI 映射，不会留下由任务长期持有的失效指针。

四项必需资源任一缺失、类型或 Metadata 不匹配、CRC 错误、目标容量不足或 Cache Clean 失败，都会阻止 APP 启动任务，并保留在诊断快照中。

FONT 是可选资源（RES-5、RES-10）：同样的任何失败只记一条 WARN `Optional resource unavailable: id=5, stage=S, component=C.`（阶段与状态编号同诊断快照），然后继续启动；诊断快照与 `IsReady()` 不受影响，`Service_Resource_GetFont()` 报告不可用，曲名回退 Montserrat。资源包仍是 v2（没有 FONT 条目）时走 `FIND_ENTRY` 阶段的 WARN。成功路径与其他资源相同：复制、目标 CRC、Clean Cache。每项成功的 INFO 带单项耗时（`time=T us`，含 Metadata 校验、复制、CRC 与 Clean），供板测核对 FONT 的启动开销；Init 结束另记总耗时。

### FONT 区的 HWD-2 审查

- 启动顺序：`.sdram_font_resource` 是 NOLOAD，由本 Module 在 Platform SDRAM 正式初始化之后、任务启动之前一次写完；破坏性 SDRAM 诊断只在业务占用前运行。复制或 CRC 失败时不发布给消费者。
- MPU：位于 `0xC0000000` 32 MiB 可缓存写回区域内，只放数据，不需要新区域。
- Cache：CPU 复制后按 32 B 取整 Clean（RES-6）；区起点 32 B 对齐、大小为 32 的倍数，取整不越出本区。消费者经 Cache 只读，同一 CPU 写读，不需要 Invalidate；不作 DMA 源或目标。
- 容量：0xA4800 = 673,792 B，入库字体 bin 623,520 B（余量 50,272 B；Pack v4 起比 v3 扩 64 KiB）；链接脚本断言大小，运行期仍核对目标容量，超出时走可选资源 WARN。

来源地址、容量、产品身份、资源 ID 和日志开关集中在 `resource_service_config.h`。完整链路见 [RPKC1 设计](../../docs/shape/resources.md)。设备侧从 SD 更新壁纸或模型时，须先经 FTL 暂存再写 Pack，见 [ADR-0015](../../docs/adr/0015-volume-roles-and-resource-install.md)；本 Module 当前仍只做启动一次性加载。

## 验证

可选路径不做主机测试，进板级验收（启动日志：只烧固件、板上仍为资源包 v2 时应见 FONT 的 WARN 与 GUI 的字体回退 WARN；写入 v3 或 v4 后应见 `Loaded id=5` 与 GUI 的 `Title font ready`）。模拟器以替身实现 `Service_Resource_GetFont()`（`Tools/gui_simulator/fakes/resource_sim.c`），覆盖消费者侧的可用与不可用两条路径。
