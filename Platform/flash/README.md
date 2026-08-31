# Platform Flash

本 Module 装配当前 PCB 上的 W25Q256 外部 NOR Flash 与 CubeMX QSPI。启动阶段绑定 STM32 HAL QSPI Adapter，读取 JEDEC ID，并按本板配置校验 Winbond 厂商码 `EF` 与 256 Mbit 容量码 `19`；随后以 `0x5A` 校验 SFDP 签名，确保 SR2.QE 已开启，并以一次从地址 0 开始的 `0xEC` Quad I/O 读取验证四线数据通路。它保留同步读取路线，并为已订阅的单一上层消费者提供 QSPI/MDMA 非阻塞读取、以及页编程/扇区擦除完成后的自动状态匹配收尾；也可读取实时 SR1/SR2 快照，并在显式诊断入口中固定操作 ADR-0009 保留的首尾两个 4 KiB 自检扇区。它还统一管理 H7 QSPI 只读内存映射窗口。FTL 与逻辑扇区能力已接入，FatFs 由 Service 管理；不提供通用物理擦写或 USB MSC。

## 预期公开 Interface

- `Platform_Flash_Init()`：绑定 QSPI Adapter，同步校验 JEDEC ID、SFDP 签名和 `0xEC` Quad I/O 读取通路；若 QE 为 0，则在启动期安全置位并回读核验；
- `Platform_Flash_GetJedecID()`：取得初始化阶段缓存的三字节 ID，不重新访问 QSPI。
- `Platform_Flash_ReadStatusRegisters()`：实时读取 SR1、SR2 与 WIP/WEL/QE；不返回缓存。
- `Platform_Flash_ReadArray()`：以 W25Q256 固定 `0xEC` 事务读取物理数组；仅供当前启动验证与 APP 基准，首地址必须 4-byte 对齐。
- `Platform_Flash_EnableMemoryMappedMode()`：在 Flash 空闲且没有在飞操作时打开 H7 `0x90000000` 的只读窗口，返回窗口首地址和 32 MiB 有效范围；不接受任意协议、地址或 QSPI Handle。
- `Platform_Flash_SetOperationCallback()` / `ClearOperationCallback()`：设置或移除唯一的 QSPI 异步操作 IRQ 订阅者；回调只能执行 FromISR 安全的轻量通知。
- `Platform_Flash_StartReadArray()` / `ProcessOperation()`：启动一次 `0xEC` MDMA 读取，并在收到通知后于普通上下文完成 Component 状态推进和 D-Cache 收尾。
- `Platform_Flash_StartDiagnosticRead()` / `ReadDiagnostic()` / `VerifyDiagnosticReadBuffer()`：只按“首/尾”区域语义对固定自检扇区执行 MDMA 或同步读回，并比较 Platform 私有的地址相关图样；调用者不能传递自检物理地址或图样。
- `Platform_Flash_FillDiagnosticBuffer()` / `StartDiagnosticErase()` / `StartDiagnosticPageProgram()`：只按“首/尾”区域语义产生图样、提交 `0x21` 擦除或提交单页 `0x34` 编程。操作完成由 Status Match IRQ 通知，调用者随后必须调用 `ProcessOperation()`；它们不是任意地址的公开擦写 Interface。

## 编译期依赖与装配

- `Components/w25qxx` 的公开 Interface；
- `Adapters/stm32_hal/w25qxx_qspi` 的 QSPI Bind Interface；
- `Adapters/stm32_hal/irq/stm32_qspi_irq` 的 Handle 局部 QSPI 回调分发 Interface；
- CubeMX 管理的 QSPI Handle、GPIO 与时钟配置。

Platform 当前长期持有 W25Qxx Handle、`EF / 19` 的期望标识、QSPI Adapter Context 和 QSPI IRQ 节点。启动识别成功后注册 `hqspi` 的读完成、自动轮询状态匹配、错误和中止回调；`MemoryType`（例如 `40` 或 `70`）只保留为诊断信息，不影响 W25Q256 兼容性判定。QE 已开启时不写 Flash；QE 为 0 时的 SR2 配置最多阻塞 20 ms，且仅发生在调度器启动前。`0xEC` 启动自检只读取物理地址 0 的 4 字节，不解释或记录内容。异步路线只有在已设置上层回调、且无其他操作在飞时才允许启动：IRQ 先更新 Adapter 结果，再转发轻量事件；拥有请求的任务随后调用 `ProcessOperation()`，MDMA 成功后数据才可被 CPU 使用，写擦成功后 Device 才回到 READY。当前唯一上层订阅者是 Filesystem Service Flash 执行器，在 Storage Task 启动时建立长期订阅；APP storage_flash 只转交诊断，不由 benchmark 临时设置或清除回调。受限自检通过首/尾区域语义请求擦除、逐页编程和读回；每笔 `0x34/0x21` 提交后由 Adapter 自动轮询 `0x05` 的 WIP 位，Status Match 唤醒 Storage Task，Platform 自身绝不等待任务通知或访问 DMA 缓冲区。两条读回共同验证后，诊断入口仍不能被 FTL 或 MSC 当作通用写入能力。当前已持有 Flash FTL Handle、Bridge Context、78 KiB SDRAM 表和 4096/512 B AXI SRAM 工作/校验区，绑定完整物理分区。

## 运行时请求与约束

- 上层只使用 `Platform_Flash_*` 所表达的板级 Flash 能力，不访问 QSPI Handle、W25Qxx Handle 或 FTL 私有元数据；当前的 `ReadArray()` 是启动诊断受限的物理读取接缝，不是逻辑地址 API；
- 非阻塞读取缓冲区必须可被 MDMA 访问，首地址和长度均按 32-byte Cache line 对齐；从 `StartReadArray()` 成功到 `ProcessOperation()` 返回前，任何 CPU 或 DMA 都不得读取、写入或复用该区；页编程数据在 `StartDiagnosticPageProgram()` 返回后直至 `ProcessOperation()` 成功前同样不得改写或复用；
- 自检入口固定使用 ADR-0009 的两个 4 KiB 扇区，并依赖调用者在启动诊断窗口独占 Flash；正常功能、资源包、镜像槽和 FTL 都不得传入或复用这些物理范围；
- 本 Module 统一切换 QSPI 内存映射：`Platform_Flash_EnableMemoryMappedMode()` 仅在确认 `SR1.WIP=0`、无间接事务且无异步操作在飞时开放固定的 `0x90000000`、32 MiB 只读窗口。映射模式不会自动查询或等待 WIP；任何同步或异步间接读取、状态读取、页编程或擦除都会先退出映射。同步操作成功后立即恢复；异步操作只在 `ProcessOperation()` 成功收尾、且写擦的自动轮询已确认 WIP 清零后恢复。失败路径保持映射关闭，避免在 NOR 状态未知时继续访问窗口；
- 映射消费者只可读取返回的有效范围，且不得跨越后续间接操作期间保留、解引用或缓存该指针。它们不应为每次读取自行发送状态查询；Platform 的模式切换是整个窗口有效的唯一保证；
- 本 Module 不实现 W25Q 指令、FTL 映射、FatFs 挂载、USB MSC 所有权、媒体扫描或任务策略；
- QSPI 实例、引脚、时钟和启动顺序是本 PCB 的事实，必须由 Platform 管理并与 CubeMX 配置同步核对。

## FTL 装配（已实现）

原始诊断与逻辑卷共用唯一执行上下文。按 [FTL 设计](../../docs/flash_ftl_design.md)，本 Module 长期持有 FTL/Bridge、SDRAM 映射表与内部 SRAM 工作区，注入经过边界核验的分区；不向 Service 暴露原始任意地址擦写。

唯一上层回调所有者已从 APP `storage_flash` 迁移到 Filesystem Service 的 Flash 执行器，仍在 Storage Task 上下文完成。Process 按当前操作派发到原始 W25Qxx 或 FTL，不重复推进同一底层操作。映射关闭覆盖整个 FTL 请求，不能每页完成就重新打开；错误时先确保控制器/DMA 不再访问缓冲，再显式恢复，NOR 内部仍忙不能视为已取消。

诊断范围仍受 ADR-0009 限制，不能借自检接口操作 FTL。具体容量、内存和启动顺序在设计文档维护，主机与构建验证通过，实际 QSPI/MDMA 和掉电验收待完成。
