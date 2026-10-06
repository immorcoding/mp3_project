# 技术文档

本目录记录跨目录、跨 Module 的稳定设计决定；局部职责和调用约束由源码目录中的 README 维护。

## 文档层级

| 层级 | 载体 | 记录内容 |
| --- | --- | --- |
| 工作合同 | `../AGENTS.md` | agent 硬规则、验证入口与指针；始终注入。 |
| 标准层 | `shape/` | 各领域当前标准（harness、workflow、architecture、gui、git、code-style），带级别与来源；按领域读。 |
| 事项层 | GitHub Issues | 进度、spec、ticket、wayfinder 地图与置顶「板上状态」；不在仓库文件里。 |
| 术语层 | `../GLOSSARY.md` | 项目特有概念及区别，每条一两句；按条查阅。 |
| 阅读/审阅入口 | `architecture_standard.md`、`coding_standard.md` | 按任务指向 shape 规则与技术正文，不另存规则。 |
| 决策层 | `adr/` | 可替代方案中的长期取舍、未采用方案和演进后果。 |
| 技术事实层 | `*_architecture.md`、`error_model.md` | 当前目录、调用链、状态机、参数、资源和验收约束。 |
| 局部导航层 | 各源码目录 `README.md` | Module 公开 Interface、直接依赖、运行时路径和禁止事项。 |

ADR 不替代技术文档。技术文档引用相关 ADR，并保持对当前代码的准确描述；新增长期取舍时先补 ADR，再同步受影响的技术文档、`GLOSSARY.md` 和 Module README。

## 入口

- [../AGENTS.md](../AGENTS.md)：硬规则、验证入口与指针；
- [shape/](shape/)：各领域当前标准；[ROUTES](shape/ROUTES.md) 按目录引导阅读；
- [../GLOSSARY.md](../GLOSSARY.md)：项目特有概念和易混淆关系（按条查阅）；
- [architecture_standard.md](architecture_standard.md)：架构规则和跨模块技术解释的阅读入口；
- [adr/README.md](adr/README.md)：长期架构决策记录；
- [coding_standard.md](coding_standard.md)：代码审阅指针（命名、Doxygen、config）；
- [verification.md](verification.md)：FAST/CHANGED/FULL/HARDWARE、Hook 快照、结果状态、Skill 与独立审校者；
- [sd_architecture.md](sd_architecture.md)：SD、热插拔与 FatFs 接缝；
- [w25q256_architecture.md](w25q256_architecture.md)：W25Q256 原始 NOR 现状与 Adapter 接缝；
- [flash_ftl_design.md](flash_ftl_design.md)：FTL 链路、分组提交、恢复、GC、配置与故障模型；
- [filesystem_service_reshape.md](filesystem_service_reshape.md)：当前卷感知公开接缝、执行所有权与私有错误转换；
- [catalog_architecture.md](catalog_architecture.md)：曲库扫描、顺序播放列表，以及未落地的 MP3 解析/ID3 边界；
- [resource_pack_format.md](resource_pack_format.md)：打包器与解码器共享的 RPKC1 二进制契约；
- [runtime_paths.md](runtime_paths.md)：硬件完成事件、LCD DMA 与任务路径；
- [resource_pack_design.md](resource_pack_design.md)：RPKC1 资源包格式与启动加载；设备侧安装见 ADR-0015（未实现）；
- [adr/0013-usb-msc-product-scope.md](adr/0013-usb-msc-product-scope.md)：主线 USB MSC 产品范围决定；
- [adr/0014-filesystem-volume-aware-file-interface.md](adr/0014-filesystem-volume-aware-file-interface.md)：SD/Flash 统一为 Volume + UTF-8 相对路径；Flash 自动格式化由 Storage 宏控制；
- [adr/0015-volume-roles-and-resource-install.md](adr/0015-volume-roles-and-resource-install.md)：SD 为唯一音乐库；曲库与播放列表分开；Flash FTL 作机内盘与资源安装暂存；
- [adr/0016-hand-written-gui-view-and-shared-theme-styles.md](adr/0016-hand-written-gui-view-and-shared-theme-styles.md)：界面在 `Service/gui/view/` 手写，主题为共享颜色 style，取代 ADR-0007；
- [log_architecture.md](log_architecture.md)：日志核心、USB Adapter 与任务化约束；
- [pmic_i2c_architecture.md](pmic_i2c_architecture.md)：AXP2101、SoftI2C 与平台电源；
- [sdram_architecture.md](sdram_architecture.md)：FMC SDRAM 的初始化、诊断与后续使用约束；
- [temperature_architecture.md](temperature_architecture.md)：MCU 内部结温采样、工厂标定与 Platform 边界；
- [touch_architecture.md](touch_architecture.md)：FT6X36、I2C2、TP_RST、轮询式输入与后续 TP_IRQ 演进；
- [gui_ui_design.md](gui_ui_design.md)：240 x 320 GUI 的视觉规范、页面层级与交互边界；
- [../Tools/gui_simulator/README.md](../Tools/gui_simulator/README.md)：PC GUI 模拟器（SDL2）与场景回归基线；
- [error_model.md](error_model.md)：状态、错误和诊断语义。

agent 冷启动只读根目录 `AGENTS.md`（Claude Code 经 `CLAUDE.md` 引入）；`docs/shape/`、`GLOSSARY.md` 与本目录按需读取；根 `README.md` 给人看功能清单，agent 非必要不读。本目录仍是按需正文。发生影响多个 Module 的行为、Interface 或职责归属的调整时，必须同步更新相应技术文档并核对 `GLOSSARY.md`。
