# Coding standards

供 `code-review` 按本次 diff 选择适用规范；引用规则编号，指出违反 provisional 或 settled 规则的改动。正文由下列来源维护，本文件只提供评审指针。

| 变动范围 | 规范与检查依据 |
| --- | --- |
| 所有权、依赖、Ops、ISR、持久存储与模块接缝 | [Architecture](docs/shape/architecture.md) |
| 标识符、Doxygen、配置与排版 | [Code style](docs/shape/code-style.md) |
| 曲库、卷、文件系统、FTL 与介质生命周期 | [Storage](docs/shape/storage.md) |
| 资源格式、加载与安装边界 | [Resources](docs/shape/resources.md) |
| 界面、交互、效果生命周期与像素基线 | [GUI](docs/shape/gui.md) |
| 板级约束与日志生命周期 | [Hardware](docs/shape/hardware.md)、[Diagnostics](docs/shape/diagnostics.md) |
| DMA、Cache、ISR、RTOS、HAL、Platform、链接段或硬件生命周期 | [嵌入式审阅清单](docs/agents/embedded-checklist.md)及对应模块契约 |
| 验证覆盖、原始证据、主机与板级结论 | [Harness](docs/shape/harness.md#verification)、[验证入口](docs/shape/sources/verification.md) |
| 文档、shape 或工程流程调整 | [Workflow](docs/shape/workflow.md)、[Shape 结构与内容审阅](docs/shape/README.md#结构与内容审阅)、[Git](docs/shape/git.md) |

模块接口与运行路径按 [ROUTES](docs/shape/ROUTES.md) 查就近 README；需求依据为对应 spec/ticket，读取方式见 [tracker](docs/agents/issue-tracker.md)。
