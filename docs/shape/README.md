# 工程 Shape

当前规则与跨模块设计按领域阅读。每个 area 的规则决定怎样修改工程，其参考正文解释必要的格式、硬件和运行时事实；模块接口仍以就近 README 与公开头为准。

先按 [目录路由](ROUTES.md) 选择模块，再读相关领域，不整本加载所有正文。

| 领域 | 阅读内容 |
| --- | --- |
| [Architecture](architecture.md) | 分层、装配、生命周期和构建边界；[运行路径](architecture.runtime.md) |
| [Code style](code-style.md) | 命名、Doxygen、配置；[审阅入口](REVIEW.md) |
| [Storage](storage.md) | 曲库、列表、文件系统、FTL 与介质生命周期 |
| [Resources](resources.md) | 资源包、加载与安装边界，格式兼容性 |
| [GUI](gui.md) | 视图、外观、交互、视觉效果与场景回归 |
| [Hardware](hardware.md) | 电源、SDRAM、触摸和结温的板级约束 |
| [Diagnostics](diagnostics.md) | 错误解释、日志缓冲与输出生命周期 |
| [Harness](harness.md) | Agent 配置、守卫与验证；[验证参考](harness.verification.md) |
| [Workflow](workflow.md) | spec/ticket、文档职责、交接与板级证据 |
| [Git](git.md) | 分支、提交与 PR |

术语按条查根 [GLOSSARY](../../GLOSSARY.md)；历史取舍查 [ADR 索引](../adr/README.md)。事项、进度与验收证据在 GitHub，读取约定见 [tracker](../agents/issue-tracker.md)；领域词典与 ADR 的职责见 [domain](../agents/domain.md)。

新增内容先判断所属 area；重复正文由主要来源承接并删除，引用者同步更新。目录路由描述事实，规则与参考资料分别维护，不把硬件参数或历史流水写成新规则。
