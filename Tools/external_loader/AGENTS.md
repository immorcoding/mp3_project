# External Loader 工作约定

继承根入口。修改前读 [本工具说明](README.md)；Flash 分区变化另读 [ADR-0009](../../docs/adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md) 与 [资源布局](../../docs/resource_pack_design.md)。独立轮询路线、CubeProgrammer ABI、`.info`、AXI SRAM 布局与物理地址窗口以 README 为准。

- `src/`、`include/`、CMake 和工具链为自维护内容；`third_party/` 保留来源、许可和上游头，非必要不改。
- 擦写物理 Flash 时不得用测试或默认操作覆盖固件槽、诊断扇区、Resource Pack 或 FTL 数据区。
- 几何变化运行 README 的 host 测试；生产源码、头、CMake、工具链或链接脚本变化另交叉构建 `.stldr` 并调用 `embedded-reviewer`。
- 几何测试不等于 `.stldr` 构建或真机验证；生产变化保持 `NEEDS_HARDWARE_VALIDATION`，直到取得 CubeProgrammer 连接、读写与校验证据。
