# 代码审阅入口

命名、注释与配置规则统一维护在 [Code style](shape/code-style.md)。本页只给审阅指针；审阅者应指出违反相关 provisional 规则的代码，并引用规则编号。

| 审阅范围 | 应引用的规则正文 |
| --- | --- |
| 命名：Module 前缀、公开/私有符号、Task、类型 | [Code style · naming](shape/code-style.md#naming) |
| 注释：定义处 Doxygen、参数/返回、并发和失败契约 | [Code style · comments](shape/code-style.md#comments) |
| 配置：宏所有权、唯一 config、分组与对齐 | [Code style · config](shape/code-style.md#config) |
| 架构：层级、生成器、Ops、ISR、持久存储 | [Architecture](shape/architecture.md) |
| 构建：host 隔离、include、组件目标依赖 | [Architecture · maintenance](shape/architecture.md#maintenance) |
| GUI：view、主题与像素基线 | [GUI](shape/gui.md) |
| 验证与工具配置 | [Harness](shape/harness.md) |
| 提交标题与分支 | [Git](shape/git.md) |

按 [目录路由](shape/ROUTES.md) 读取被修改 Module 的 README 和相关技术说明；验收命令见 [verification.md](verification.md)。
