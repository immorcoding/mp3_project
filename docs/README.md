# 技术文档

本目录记录跨目录、跨 Module 的稳定设计决定；局部职责和调用约束由源码目录中的 README 维护。

## 文档层级

| 层级 | 载体 | 记录内容 |
| --- | --- | --- |
| 术语层 | `../CONTEXT.md` | 稳定领域术语、产品职责与入口。 |
| 规范层 | `architecture_standard.md`、`coding_standard.md` | 全工程必须遵守的分层、Interface、命名与注释规则。 |
| 决策层 | `adr/` | 可替代方案中的长期取舍、未采用方案和演进后果。 |
| 技术事实层 | `*_architecture.md`、`error_model.md` | 当前目录、调用链、状态机、参数、资源和验收约束。 |
| 局部导航层 | 各源码目录 `README.md` | Module 公开 Interface、直接依赖、运行时路径和禁止事项。 |

ADR 不替代技术文档。技术文档引用相关 ADR，并保持对当前代码的准确描述；新增长期取舍时先补 ADR，再同步受影响的技术文档、`CONTEXT.md` 和 Module README。

## 入口

- [../CONTEXT.md](../CONTEXT.md)：稳定领域术语、产品语义和职责归属；
- [architecture_standard.md](architecture_standard.md)：功能/抽象所有权、编译期依赖、运行时请求/事件路径、装配和中断规则的唯一总则；
- [adr/README.md](adr/README.md)：长期架构决策记录；
- [coding_standard.md](coding_standard.md)：命名、Doxygen 与行内注释规则；
- [sd_architecture.md](sd_architecture.md)：SD、热插拔与 FatFs 接缝；
- [w25q256_architecture.md](w25q256_architecture.md)：W25Q256 原始 NOR 现状与 Adapter 接缝；
- [flash_ftl_design.md](flash_ftl_design.md)：已实现、待硬件验收的首版 FTL 链路、分组提交、恢复、GC、配置和逐文件实施/验收清单；
- [filesystem_service_reshape.md](filesystem_service_reshape.md)：Filesystem 公开接缝切开、Maintain 改名 Reclaim；后续卷感知文件/目录见 ADR-0014；
- [adr/0013-usb-msc-product-scope.md](adr/0013-usb-msc-product-scope.md)：主线 USB MSC 产品范围决定；
- [adr/0014-filesystem-volume-aware-file-interface.md](adr/0014-filesystem-volume-aware-file-interface.md)：SD/Flash 统一为 Volume + UTF-8 相对路径；
- [log_architecture.md](log_architecture.md)：日志核心、USB Adapter 与任务化约束；
- [pmic_i2c_architecture.md](pmic_i2c_architecture.md)：AXP2101、SoftI2C 与平台电源；
- [sdram_architecture.md](sdram_architecture.md)：FMC SDRAM 的初始化、诊断与后续使用约束；
- [temperature_architecture.md](temperature_architecture.md)：MCU 内部结温采样、工厂标定与 Platform 边界；
- [touch_architecture.md](touch_architecture.md)：FT6X36、I2C2、TP_RST、轮询式输入与后续 TP_IRQ 演进；
- [gui_ui_design.md](gui_ui_design.md)：240 x 320 GUI 原型的视觉规范、页面层级与交互边界；
- [error_model.md](error_model.md)：状态、错误和诊断语义。

根目录与源码目录的 README 负责导航、局部职责和调用约束；`CONTEXT.md` 负责稳定领域术语；本目录负责跨 Module 的技术事实。发生影响多个 Module 的行为、Interface 或职责归属的调整时，必须同步更新相应技术文档并核对 `CONTEXT.md`。
