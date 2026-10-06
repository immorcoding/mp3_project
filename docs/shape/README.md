# 工程 Shape

当前标准按领域阅读。每个 area 按互斥的 title 分组，每条规则说明约束、理由与验收方式；模块接口与运行路径查就近 README 和公开头。

先按 [目录路由](ROUTES.md) 选择模块，再读相关领域，不整本加载所有正文。

| 领域 | 阅读内容 |
| --- | --- |
| [Architecture](architecture.md) | 分层、装配、生命周期和构建边界 |
| [Code style](code-style.md) | 命名、Doxygen、配置；[审阅入口](REVIEW.md) |
| [Storage](storage.md) | 曲库、列表、文件系统、FTL 与介质生命周期 |
| [Resources](resources.md) | 资源包、加载与安装边界，格式兼容性 |
| [GUI](gui.md) | 视图、外观、交互、视觉效果与场景回归 |
| [Hardware](hardware.md) | 电源、SDRAM、触摸和结温的板级约束 |
| [Diagnostics](diagnostics.md) | 错误解释、日志缓冲与输出生命周期 |
| [Harness](harness.md) | Agent 配置、守卫与验证；[验证参考](sources/verification.md) |
| [Workflow](workflow.md) | spec/ticket、文档职责、交接与板级证据 |
| [Git](git.md) | 分支、提交与 PR |

术语按条查根 [GLOSSARY](../../GLOSSARY.md)；历史取舍查 [ADR 索引](../adr/README.md)。事项、进度与验收证据在 GitHub，读取约定见 [tracker](../agents/issue-tracker.md)；领域词典与 ADR 的职责见 [domain](../agents/domain.md)。

新增标准先确定 area 与 title 的适用范围，再提炼规则；不按旧文档章节搬运。仅超过 15 条规则的 area 才整体按 title 拆分；必要的硬件事实、介质字节契约与命令表放在 `sources/`，由规则引用。References 放示例或批准的视觉依据，历史由 Git/ADR 保存。
