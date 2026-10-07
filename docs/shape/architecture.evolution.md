# Architecture · evolution

模块所有权判定、构建封装与长期决策。

[返回 Architecture](architecture.md)

### Rules

- **ARC-4** · settled · 长期约束 Module 所有权、Interface 接缝、并发或资源生命周期的决定写 ADR；改变现行规则时同步 writer branch 的 shape，ADR 与技术文档不能各自发布冲突规则。 _Why:_ 保留决策原因，同时让当前标准只有一个约束源。 _Source:_ [domain.md](../agents/domain.md) _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-13** · settled · 新增 Module 按可移植核心、SDK 转换、PCB 装配、产品流程、顶层启动判定 Components/Adapters/Platform/Service/APP 所有权；README 分别说明职责、资源与抽象所有权、公开接口、编译依赖、请求/事件/ISR 路径、禁止依赖与生命周期。边界不清先定 ADR/shape，不靠目录命名代替决定。 _Why:_ 目录位置不能替代接口与资源所有权的设计。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §12 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-14** · settled · 主机 Tests 独立 CMake，只编译待测核心和 Fake Adapter，不进入固件递归收集；项目 include 从工程根写完整路径。组件需跨仓复用时才增加独立 target，公开自己的 include、PRIVATE 实现依赖、PUBLIC 公共头暴露的依赖，不依赖固件目标的隐式 include。 _Why:_ 主机测试和可复用组件不能隐式依赖固件构建环境。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §2、§11 _Check:_ `docs/shape/REVIEW.md` 的构建审阅入口。
