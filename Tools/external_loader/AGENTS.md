# External Loader 工作约定

本文件对 `Tools/external_loader/` 全部子目录生效，并继承根 `AGENTS.md`。开始修改前完整阅读本目录 `README.md`；涉及 Flash 分区时再读取 ADR-0009 和资源布局文档。

## 本地规则

- 本目录产出 STM32CubeProgrammer 临时加载到 AXI SRAM 的 `.stldr`，不是主固件 Module。
- 保持 CubeProgrammer ABI、`.info` 段、AXI SRAM 链接布局、入口返回约定和 W25Q256 物理地址窗口一致。
- `src/`、`include/`、CMake 与工具链是项目自维护内容；`third_party/` 保留来源、许可和上游文件头，非必要不改。
- Loader 使用独立轮询 HAL 路线，不引入主固件的 FreeRTOS、FTL、Service 或异步 MDMA 生命周期。
- 擦写范围是物理 Flash；禁止用测试或默认操作覆盖固件槽、诊断扇区、Resource Pack 或 FTL 数据区。
- `tests/loader_geometry_test.c` 是纯 host 几何测试，不等于 `.stldr` 构建或真机 CubeProgrammer 验证。

## 完成条件

- 几何变化先运行 README 中的 host 测试；生产源码、头、CMake、工具链或链接脚本变化还要交叉构建 `.stldr`。
- 上述生产变化必须调用只读 `embedded-reviewer`，最终保持 `NEEDS_HARDWARE_VALIDATION`，直到完成 CubeProgrammer 连接、读写和校验证据。
