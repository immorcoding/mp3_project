# Architecture · ownership

各层的抽象、板级资源和产品策略归属。

[返回 Architecture](architecture.md)

### Rules

- **ARC-1** · settled · Component 拥有可复用算法、芯片协议、状态机及它消费的 Ops；Adapter 实现 Ops 并可包含 Component 公共头；Platform 包含 Component、Adapter 和 CubeMX 头完成本板装配；Component 不包含 Adapter 私有头、生成实例、HAL、USB 或 FreeRTOS API，Adapter/Component 不包含 Service/APP 头或主动调用产品业务。功能层次不要求每次请求经过全部层；已声明的第三方入站接缝不构成反向 include 许可。 _Why:_ 抽象由消费方拥有，运行时回调不能反推编译依赖。 _Source:_ [ADR-0002](../adr/0002-layering-and-interface-ownership.md) _Check:_ `scripts/check-layer-includes.ps1` 强制 include 方向；语义按 `docs/shape/REVIEW.md` 审阅。
- **ARC-5** · settled · Adapter 转换 SDK/原始状态和具体 Context，不选择 PCB Handle、引脚、供电或启动策略，也不偷偷绑定全局实例；`bridge/` 只转换 Component 接口，`cortex/` 只封装内核能力且不持有外设/任务状态，`stm32_hal/` 集中 HAL/Handle/全局回调；FreeRTOS 项目配置与 Hook 留在 `Config/`，内核 `Source/` 保持上游。 _Why:_ 同一协议适配应能替换板级资源，不夹带产品策略。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §3.2 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-6** · settled · Platform 是唯一板级对象装配层，持有 Component Handle/Adapter Context，注入 PCB 资源、极性和启动等待，向上只发布产品能力；APP 不见寄存器、HAL Handle、USB 状态和 Component 私有 Handle。SDRAM Platform 负责 JEDEC 初始化，不拥有链接段、堆、帧缓冲或任务策略。 _Why:_ 板级装配集中才能隔离引脚、时序和资源实例变化。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §3.4 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-7** · settled · Service 经 Platform、Component 和必要 Middleware 的公开接口编排产品流程，不依赖 HAL；APP 负责启动顺序、失败策略、任务与 Service 连接，不构造 Ops、填写 Component Handle、解释 HAL 状态或访问寄存器。Storage Task 与未来 Service/storage 的产品服务职责分开。 _Why:_ 启动策略与可复用产品流程分别演进，避免透传硬件细节。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §2、§3.5–3.6 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
