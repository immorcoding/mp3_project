# AGENTS.md

## 项目说明

本项目是基于 STM32H743 的便携式媒体播放器固件。

当前主要功能包括：

- AXP2101 电源管理；
- PCM5102A I2S 音频输出；
- SDMMC SD 卡访问、热插拔，以及由 Filesystem Service 持有、在 Storage Task 上下文执行的同步 DMA FatFs Bridge；
- USB CDC 日志；
- FreeRTOS；
- FatFs 文件系统的挂载、卸载和显式格式化；
- 32 MiB 外部 SDRAM；
- ST7789 LCD、FT6X36 触摸与 LVGL v8.3.11 GUI 原型；
- W25Q256 QSPI Flash 的间接模式 JEDEC ID 与 SFDP 启动识别；
- 后续将加入音频解码、Flash 原始擦写/FTL 和 USB MSC 所有权切换。

技术文档和代码注释统一使用中文。

## GUI 与 SquareLine 规则

`GUI/` 是 SquareLine Studio 的生成目录，SquareLine 工程与其模拟器是 GUI 原型的唯一事实来源。除非用户明确撤销此约束，禁止直接修改其中的生成代码、生成配置或资源清单；助手只能给出 SquareLine 编辑器中的组件、布局、样式和事件配置步骤，由用户编辑、验证并导出。

GUI 设计每推进一步，必须先同步更新 `docs/gui_ui_design.md`，再开始下一步 SquareLine 操作。页面、组件层级、坐标、视觉规范、手势或事件归属、动画、状态切换和原型范围的任何确认或修订都属于一次设计推进；待验证方案必须在文档中明确标记，模拟器或硬件验证推翻既有决定时也必须先修正文档。

## 开始工作前

涉及架构、模块边界或公共接口修改前，必须先阅读：

- `CONTEXT.md`
- `docs/architecture_standard.md`
- 与当前模块对应的 `docs/*.md`

不要仅根据目录名称推测职责，应结合现有代码和文档判断。

开始跨层设计或修改前，必须分别判断：

1. 功能/抽象所有权属于哪一层；
2. 编译期需要包含或链接谁拥有的公开 Interface；
3. 运行时请求向下如何进入、外部事件经哪个已注册回调向上发布。

禁止从功能分层图直接推导 `#include` 方向。具体规则以 `docs/architecture_standard.md` 为准，目录 README 记录局部 Module 的三类路径和资源约束。

## 工程分层

工程主要遵循以下逻辑分层：

```text
Vendor/HAL
    ↓
Adapters
    ↓
Components
    ↓
Platform
    ↓
Service
    ↓
APP
```

具体的情况参考每个工程目录下的 分层.png

## 完成前验收

未运行且通过 `./scripts/verify.ps1`，不得声称工作完成。该命令会先做分层 `#include` 检查，再构建固件 Debug/Release 并跑主机回归。只改 `Components/`、`Adapters/bridge/` 或 `Service/` 的包含关系时，可先单独运行 `./scripts/check-layer-includes.ps1`。

克隆后在仓库根目录执行一次 `./scripts/install-git-hooks.ps1`，提交时由 `pre-commit` 自动跑分层检查，失败则拒绝提交。助手禁止使用 `git commit --no-verify`；仅维护者本人在本机命令行显式带上该参数时可以绕过 hook。

## Agent skills

### Issue tracker

需求说明和可执行 issue 使用本地 Markdown 维护，放在 `.scratch/<feature-slug>/` 下。具体格式见 `docs/agents/issue-tracker.md`。

### Triage labels

本项目使用固定的本地状态标签来标记需求和 issue 的处理状态。具体定义见 `docs/agents/triage-labels.md`。

### Domain docs

项目领域上下文以根目录 `CONTEXT.md` 为唯一入口；如需记录非显然、会长期约束后续重构的架构决策，则在 `docs/adr/` 新建 ADR。具体读取规则见 `docs/agents/domain.md`。
