# 技术文档

本目录记录跨目录、跨 Module 的稳定设计决定；局部职责和调用约束由源码目录中的 README 维护。

## 文档层级

| 层级 | 载体 | 记录内容 |
| --- | --- | --- |
| 工作合同 | `../AGENTS.md` | agent 硬规则、验证入口与指针；始终注入。 |
| 标准层 | `shape/` | 各领域当前标准（harness、workflow、architecture、git、code-style），带级别与来源；按领域读。 |
| 事项层 | GitHub Issues | 进度、spec、ticket、wayfinder 地图与置顶「板上状态」；不在仓库文件里。 |
| 术语层 | `../CONTEXT.md` | 稳定领域术语、产品职责与入口；按条查阅。 |
| 规范层 | `architecture_standard.md`、`coding_standard.md` | 全工程必须遵守的分层、Interface、命名与注释规则。 |
| 决策层 | `adr/` | 可替代方案中的长期取舍、未采用方案和演进后果。 |
| 技术事实层 | `*_architecture.md`、`error_model.md` | 当前目录、调用链、状态机、参数、资源和验收约束。 |
| 局部导航层 | 各源码目录 `README.md` | Module 公开 Interface、直接依赖、运行时路径和禁止事项。 |

ADR 不替代技术文档。技术文档引用相关 ADR，并保持对当前代码的准确描述；新增长期取舍时先补 ADR，再同步受影响的技术文档、`CONTEXT.md` 和 Module README。

## 入口

- [../AGENTS.md](../AGENTS.md)：硬规则、验证入口与指针；
- [shape/](shape/)：各领域当前标准；
- [../CONTEXT.md](../CONTEXT.md)：稳定领域术语、产品语义和职责归属（按条查阅）；
- [architecture_standard.md](architecture_standard.md)：功能/抽象所有权、编译期依赖、运行时请求/事件路径、装配和中断规则的唯一总则；
- [adr/README.md](adr/README.md)：长期架构决策记录；
- [coding_standard.md](coding_standard.md)：命名、Doxygen、行内注释、自维护 config 排版；
- [verification.md](verification.md)：FAST/CHANGED/FULL/HARDWARE、Hook 快照、结果状态、Skill 与独立审校者；
- [sd_architecture.md](sd_architecture.md)：SD、热插拔与 FatFs 接缝；
- [w25q256_architecture.md](w25q256_architecture.md)：W25Q256 原始 NOR 现状与 Adapter 接缝；
- [flash_ftl_design.md](flash_ftl_design.md)：已实现、待硬件验收的首版 FTL 链路、分组提交、恢复、GC、配置和逐文件实施/验收清单；
- [filesystem_service_reshape.md](filesystem_service_reshape.md)：Filesystem 公开接缝切开、Maintain 改名 Reclaim；后续卷感知文件/目录见 ADR-0014；
- [catalog_architecture.md](catalog_architecture.md)：曲库扫描、顺序播放列表，以及未落地的 MP3 解析/ID3 边界；
- [resource_pack_design.md](resource_pack_design.md)：RPKC1 资源包格式与启动加载；设备侧安装见 ADR-0015（未实现）；
- [adr/0013-usb-msc-product-scope.md](adr/0013-usb-msc-product-scope.md)：主线 USB MSC 产品范围决定；
- [adr/0014-filesystem-volume-aware-file-interface.md](adr/0014-filesystem-volume-aware-file-interface.md)：SD/Flash 统一为 Volume + UTF-8 相对路径；Flash 自动格式化由 Storage 宏控制；
- [adr/0015-volume-roles-and-resource-install.md](adr/0015-volume-roles-and-resource-install.md)：SD 为唯一音乐库；曲库与播放列表分开；Flash FTL 作机内盘与资源安装暂存；
- [log_architecture.md](log_architecture.md)：日志核心、USB Adapter 与任务化约束；
- [pmic_i2c_architecture.md](pmic_i2c_architecture.md)：AXP2101、SoftI2C 与平台电源；
- [sdram_architecture.md](sdram_architecture.md)：FMC SDRAM 的初始化、诊断与后续使用约束；
- [temperature_architecture.md](temperature_architecture.md)：MCU 内部结温采样、工厂标定与 Platform 边界；
- [touch_architecture.md](touch_architecture.md)：FT6X36、I2C2、TP_RST、轮询式输入与后续 TP_IRQ 演进；
- [gui_ui_design.md](gui_ui_design.md)：240 x 320 GUI 原型的视觉规范、页面层级与交互边界；
- [error_model.md](error_model.md)：状态、错误和诊断语义。

agent 冷启动只读根目录 `AGENTS.md`（Claude Code 经 `CLAUDE.md` 引入）；`docs/shape/`、`CONTEXT.md` 与本目录按需读取；根 `README.md` 给人看功能清单，agent 非必要不读。本目录仍是按需正文。发生影响多个 Module 的行为、Interface 或职责归属的调整时，必须同步更新相应技术文档并核对 `CONTEXT.md`。
