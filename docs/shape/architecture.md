# Architecture

分层、Interface、装配与并发的当前规则；硬件和调用链事实按 [文档地图](README.md) 阅读。

Next id: ARC-15

## Pillars

- 功能所有权、编译期依赖、运行时路径是三张图，不能互相推导。
- 生成器维护产物，HAL 差异在 Adapter 内吸收。
- 公开 Interface 隐藏实现，装配与执行分开。

## layering

目录所有权与允许依赖。

### Rules

- **ARC-1** · settled · Component 拥有可复用算法、芯片协议、状态机及它消费的 Ops；Adapter 实现 Ops 并可包含 Component 公共头；Platform 包含 Component、Adapter 和 CubeMX 头完成本板装配；Component 不包含 Adapter 私有头、生成实例、HAL、USB 或 FreeRTOS API，Adapter/Component 不包含 Service/APP 头或主动调用产品业务。功能层次不要求每次请求经过全部层；已声明的第三方入站接缝不构成反向 include 许可。_Source:_ [ADR-0002](../adr/0002-layering-and-interface-ownership.md) _Check:_ `scripts/check-layer-includes.ps1` 强制 include 方向；语义按 `docs/shape/REVIEW.md` 审阅。
- **ARC-2** · settled · CubeMX（`io_sheet.ioc` 产物）和 Vendor（HAL/CMSIS/中间件）只经源工程重新导出或上游更新；HAL 缺陷在 `Adapters/stm32_hal/` 绕过。界面按 GUI-1 手写；`FATFS/Target/bsp_driver_user_diskio.*` 为自维护契约。_Why:_ 重新导出覆盖手改。_Check:_ Agent Hook 保护全部生成/Vendor 产物；`check-generated-write.ps1` 保护 Vendor 与 `cmake/stm32cubemx/`。
- **ARC-3** · settled · CubeMX `USER CODE` 修改先获用户确认，只调用或转发自维护入口，不放产品逻辑；链接段要求 C 运行库前访问外部存储时，`fmc.c` 的早期初始化接缝可使用局部状态与 Vendor/HAL，但不进入 APP/Service/Platform/FreeRTOS，不作为产品 Interface。_Source:_ [ADR-0008](../adr/0008-sdram-early-init-and-noload-ownership.md) _Check:_ Agent Hook 对 USER CODE 文件走 ask（Codex deny）；例外范围按 `docs/shape/REVIEW.md` 审阅。
- **ARC-4** · settled · 长期约束 Module 所有权、Interface 接缝、并发或资源生命周期的决定写 ADR；改变现行规则时同步 writer branch 的 shape，ADR 与技术文档不能各自发布冲突规则。_Source:_ [domain.md](../agents/domain.md) _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-5** · settled · Adapter 转换 SDK/原始状态和具体 Context，不选择 PCB Handle、引脚、供电或启动策略，也不偷偷绑定全局实例；`bridge/` 只转换 Component 接口，`cortex/` 只封装内核能力且不持有外设/任务状态，`stm32_hal/` 集中 HAL/Handle/全局回调；FreeRTOS 项目配置与 Hook 留在 `Config/`，内核 `Source/` 保持上游。_Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §3.2 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-6** · settled · Platform 是唯一板级对象装配层，持有 Component Handle/Adapter Context，注入 PCB 资源、极性和启动等待，向上只发布产品能力；APP 不见寄存器、HAL Handle、USB 状态和 Component 私有 Handle。SDRAM Platform 负责 JEDEC 初始化，不拥有链接段、堆、帧缓冲或任务策略。_Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §3.4 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-7** · settled · Service 经 Platform、Component 和必要 Middleware 的公开接口编排产品流程，不依赖 HAL；APP 负责启动顺序、失败策略、任务与 Service 连接，不构造 Ops、填写 Component Handle、解释 HAL 状态或访问寄存器。Storage Task 与未来 Service/storage 分开，未实现的 Module 不建空目录/占位 Interface；Components 保持扁平，只有稳定分类需求才增分组。_Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §2、§3.5–3.6 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。

## lifecycle

装配、错误、并发与持久存储边界。

### Rules

- **ARC-8** · settled · Ops 与 Context 成对绑定且生命周期覆盖调用；Platform 用 static 长期持有 Handle/Context；Bind 只装配且检查 Handle/Ops/Context/资源，Init 再检查必需回调后访问硬件，APP 不调用 Bind。_Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §4 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-9** · settled · 返回值表示本次调用，State 表示持续生命周期，ErrorCode 表示 Component 语义步骤，LastBusStatus/LastPortStatus 表示归一化底层原因；Vendor 原始错误仅用于深入诊断。Adapter 状态转换入口用 `int32_t native_status`，内部再转 SDK 类型，上层不据 HAL 原始码决策。_Source:_ [错误解释](diagnostics.reference.md) _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-10** · settled · ISR 只发布轻量事件/FromISR 通知，不格式化日志、发 USB、访问 I2C/SD/文件系统、延时消抖或调用普通 RTOS API；任务执行状态机与业务。GPIO EXTI 使用调用者长期持有、普通上下文注册注销的 Callback 链表；其他外设用按 Handle 的强类型回调，不扩成 `IRQ_ID + void *` 通用分发器。_Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §8；[中断设计](architecture.md) _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-11** · settled · FatFs USER 接缝由 FATFS Target 自维护契约/安全弱定义、生成区薄转发和 Service 强定义组成；Filesystem 是一个 Module，SD/Flash 私有分区，Service 持有唯一完成订阅与执行等待，Storage Task 决定时机；FTL 算法/GC/RawOps 属 Component，Platform 持有实例与映射生命周期。FatFs 不绕过 FTL 接原始 NOR；不预建 BlockDevice/Service/storage，USB MSC 不在产品范围。_Source:_ [ADR-0010](../adr/0010-fatfs-user-diskio-service-ownership.md)、[ADR-0012](../adr/0012-filesystem-public-seams.md)、[ADR-0013](../adr/0013-usb-msc-product-scope.md) _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-12** · settled · APP 文件诊断只用 Filesystem 卷感知文件接口，不含 FatFs/逻辑块；Service 持有 FIL/DIR、路径/盘符转换和同步生命周期，APP 持有图样、计时、日志策略；当前唯一 Storage Task 执行，不预建跨任务队列。删除释放 FAT 簇，不按文件地址擦 NOR；无 TRIM 时仅 LBA 后续更新才使旧物理版本失效，GC 归 FTL。_Source:_ [ADR-0014](../adr/0014-filesystem-volume-aware-file-interface.md) _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。

### References

- [完成事件与任务路径](architecture.runtime.md)：LCD 最终完成、通知槽与 QSPI 映射窗口。
- [Storage](storage.md)、[Resources](resources.md)、[Hardware](hardware.md)、[Diagnostics](diagnostics.md)：对应领域的规则与技术参考。

## maintenance

新增模块与构建边界。

### Rules

- **ARC-13** · settled · 新增 Module 按可移植核心、SDK 转换、PCB 装配、产品流程、顶层启动判定 Components/Adapters/Platform/Service/APP 所有权；README 分别说明职责、资源与抽象所有权、公开接口、编译依赖、请求/事件/ISR 路径、禁止依赖与生命周期。边界不清先定 ADR/shape，不靠目录命名代替决定。_Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §12 _Check:_ `docs/shape/REVIEW.md` 的架构审阅入口。
- **ARC-14** · settled · 主机 Tests 独立 CMake，只编译待测核心和 Fake Adapter，不进入固件递归收集；项目 include 从工程根写完整路径。组件需跨仓复用时才增加独立 target，公开自己的 include、PRIVATE 实现依赖、PUBLIC 公共头暴露的依赖，不依赖固件目标的隐式 include。_Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §2、§11 _Check:_ `docs/shape/REVIEW.md` 的构建审阅入口。
