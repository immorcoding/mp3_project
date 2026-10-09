# Architecture · ownership

各层的抽象、板级资源和产品策略归属。

[返回 Architecture](architecture.md)

### Rules

- **ARC-1** · settled · Component 拥有可复用算法、芯片协议、状态机及它消费的 Ops；Adapter 实现 Ops 并可包含 Component 公共头；Platform 包含 Component、Adapter 和 CubeMX 头完成本板装配；Component 不包含 Adapter 私有头、生成实例、HAL、USB 或 FreeRTOS API，Adapter/Component 不包含 Service/APP 头或主动调用产品业务。功能层次不要求每次请求经过全部层；已声明的第三方入站接缝不构成反向 include 许可。 _Why:_ 抽象由消费方拥有，运行时回调不能反推编译依赖。 _Source:_ [ADR-0002](../adr/0002-layering-and-interface-ownership.md) _Check:_ `scripts/check-layer-includes.ps1` 强制 include 方向；语义按 `CODING_STANDARDS.md` 审阅。
- **ARC-5** · settled · Adapter 转换 SDK/原始状态和具体 Context，不选择 PCB Handle、引脚、供电或启动策略，也不偷偷绑定全局实例；`bridge/` 只转换 Component 接口，`cortex/` 只封装内核能力且不持有外设/任务状态，`stm32_hal/` 集中 HAL/Handle/全局回调；FreeRTOS 项目配置与 Hook 留在 `Config/`，内核 `Source/` 保持上游。 _Why:_ 同一协议适配应能替换板级资源，不夹带产品策略。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §3.2 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-6** · settled · Platform 是唯一板级对象装配层，持有 Component Handle/Adapter Context，注入 PCB 资源、极性和启动等待，向上只发布产品能力；APP 不见寄存器、HAL Handle、USB 状态和 Component 私有 Handle。SDRAM Platform 负责 JEDEC 初始化，不拥有链接段、堆、帧缓冲或任务策略。 _Why:_ 板级装配集中才能隔离引脚、时序和资源实例变化。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §3.4 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-7** · settled · Service 经 Platform、Component 和必要 Middleware 的公开接口编排产品流程，不依赖 HAL；APP 负责启动顺序、失败策略、任务与 Service 连接，不构造 Ops、填写 Component Handle、解释 HAL 状态或访问寄存器。Storage Task 与未来 Service/storage 的产品服务职责分开。 _Why:_ 启动策略与可复用产品流程分别演进，避免透传硬件细节。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §2、§3.5–3.6 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-18** · provisional · DMA 缓冲的所有权按时段转移：提交前由缓冲所有者写入；提交后由传输模块持有并负责 Cache 维护，直到完成事件或停止确认成功才交还所有者；停止失败并锁存故障时，缓冲留在传输模块直到重启。 _Why:_ ADR-0006 要求调用者拥有完整缓存行，所有权必须与持有时段一致；HWD-5 的 LCD 绘制缓冲归还属于同一模式。 _Source:_ [ADR-0006](../adr/0006-cache-range-ownership.md)、[音频 DMA、Cache 与完成事件设计](https://github.com/immorcoding/mp3_project/issues/33)、[DMA 完成事实、欠载与 Stop 失败语义](https://github.com/immorcoding/mp3_project/issues/50) _Check:_ code-review 规范轴核对提交、完成与交还的调用点。

### Signals

- 2026-10-08 · cite · ARC-18 · 音频块不是 DMA 缓冲，在空闲/就绪队列间按“持有指针者拥有”转移，不归本规则管；出现第二处同类用法时再考虑另立规则。
