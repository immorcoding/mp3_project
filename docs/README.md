# 技术文档

本目录记录跨目录、跨 Module 的稳定设计决定；局部职责和调用约束由源码目录中的 README 维护。

## 入口

- [../CONTEXT.md](../CONTEXT.md)：稳定领域术语、产品语义和职责归属；
- [architecture_standard.md](architecture_standard.md)：功能/抽象所有权、编译期依赖、运行时请求/事件路径、装配和中断规则的唯一总则；
- [coding_standard.md](coding_standard.md)：命名、Doxygen 与行内注释规则；
- [sd_architecture.md](sd_architecture.md)：SD、热插拔与 FatFs 接缝；
- [log_architecture.md](log_architecture.md)：日志核心、USB Adapter 与任务化约束；
- [pmic_i2c_architecture.md](pmic_i2c_architecture.md)：AXP2101、SoftI2C 与平台电源；
- [sdram_architecture.md](sdram_architecture.md)：FMC SDRAM 的初始化、诊断与后续使用约束；
- [temperature_architecture.md](temperature_architecture.md)：MCU 内部结温采样、工厂标定与 Platform 边界；
- [error_model.md](error_model.md)：状态、错误和诊断语义。

根目录与源码目录的 README 负责导航、局部职责和调用约束；`CONTEXT.md` 负责稳定领域术语；本目录负责跨 Module 的技术事实。发生影响多个 Module 的行为、Interface 或职责归属的调整时，必须同步更新相应技术文档并核对 `CONTEXT.md`。
