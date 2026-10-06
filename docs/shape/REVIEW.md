# 代码与文档审阅入口

审阅者引用适用的 shape 规则编号，指出违反 provisional 或 settled 规则的改动。下表只提供指针，具体约束以各领域正文为准。

| 范围 | 正文 |
| --- | --- |
| 所有权、依赖、Ops、ISR、持久存储与模块接缝 | [Architecture](architecture.md) |
| 标识符、定义处 Doxygen、config 所有权与排版 | [Code style](code-style.md) |
| 曲库、卷、文件、介质恢复与回收 | [Storage](storage.md) |
| 资源格式、加载目标与安装边界 | [Resources](resources.md) |
| 界面、主题、效果生命周期与像素基线 | [GUI](gui.md) |
| 板级硬件事实、错误与日志 | [Hardware](hardware.md)、[Diagnostics](diagnostics.md) |
| 工具配置、验证快照与证据范围 | [Harness](harness.md)、[验证参考](sources/verification.md) |
| 文档职责、状态与交付方式 | [Workflow](workflow.md)、[Git](git.md) |

按 [ROUTES](ROUTES.md) 找受影响模块的 README 和公开接口。文档变动还需检查主要正文是否唯一、规则与参考是否分开、引用能否到达、术语覆盖是否足以辨别真实概念；缩短行数不构成通过依据。

Shape 审阅逐个判断 title 是否是互斥的规则范围、每条规则能否改变未来决策且附 Why、settled 是否指明 Check；超过 15 条才整体拆分。sources 仅保留不可替代的事实/格式表；旧章节改名、重复实现讲解或把多项无关规定塞进一条规则均不通过。
