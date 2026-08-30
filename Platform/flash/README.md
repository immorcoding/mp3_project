# Platform Flash

本 Module 装配当前 PCB 上的 W25Q256 外部 NOR Flash 与 CubeMX QSPI。启动阶段绑定 STM32 HAL QSPI Adapter，读取 JEDEC ID，并按本板配置校验 Winbond 厂商码 `EF` 与 256 Mbit 容量码 `19`；随后以 `0x5A` 校验 SFDP 签名，确保 SR2.QE 已开启，并以一次从地址 0 开始的 `0xEC` Quad I/O 读取验证四线数据通路。它保留同步读取路线，并为已订阅的单一上层消费者提供 QSPI/MDMA 非阻塞读取的启动与普通上下文收尾；也可读取实时 SR1/SR2 快照，并在显式诊断入口中固定操作 ADR-0009 保留的首尾两个 4 KiB 自检扇区。Flash FTL、通用公开擦写、逻辑扇区、FatFs 和 USB MSC 均未接入。

## 预期公开 Interface

- `Platform_Flash_Init()`：绑定 QSPI Adapter，同步校验 JEDEC ID、SFDP 签名和 `0xEC` Quad I/O 读取通路；若 QE 为 0，则在启动期安全置位并回读核验；
- `Platform_Flash_GetJedecID()`：取得初始化阶段缓存的三字节 ID，不重新访问 QSPI。
- `Platform_Flash_ReadStatusRegisters()`：实时读取 SR1、SR2 与 WIP/WEL/QE；不返回缓存。
- `Platform_Flash_ReadArray()`：以 W25Q256 固定 `0xEC` 事务读取物理数组；仅供当前启动验证与 APP 基准，首地址必须 4-byte 对齐。
- `Platform_Flash_SetTransferCallback()` / `ClearTransferCallback()`：设置或移除唯一的 QSPI/MDMA 读取 IRQ 订阅者；回调只能执行 FromISR 安全的轻量通知。
- `Platform_Flash_StartReadArray()` / `ProcessTransfer()`：启动一次 `0xEC` MDMA 读取，并在收到通知后于普通上下文完成 Component 状态推进和 D-Cache 收尾。
- `Platform_Flash_StartDiagnosticRead()` / `VerifyDiagnosticReadBuffer()`：只按“首/尾”区域语义启动固定自检扇区的 MDMA 读回，并比较 Platform 私有的地址相关图样；调用者不能传递自检物理地址或图样。
- `Platform_Flash_RunDiagnostic()`：仅在调用者提供的 4 KiB 缓冲上，对 ADR-0009 固定的首、尾自检扇区执行 `0x21` 擦除、`0x34` 页编程、轮询读回和地址相关图样校验；它不是任意地址的公开擦写 Interface。

## 编译期依赖与装配

- `Components/w25qxx` 的公开 Interface；
- `Adapters/stm32_hal/w25qxx_qspi` 的 QSPI Bind Interface；
- `Adapters/stm32_hal/irq/stm32_qspi_irq` 的 Handle 局部 QSPI 回调分发 Interface；
- CubeMX 管理的 QSPI Handle、GPIO 与时钟配置。

Platform 当前长期持有 W25Qxx Handle、`EF / 19` 的期望标识、QSPI Adapter Context 和 QSPI IRQ 节点。启动识别成功后注册 `hqspi` 的读完成、错误和中止回调；`MemoryType`（例如 `40` 或 `70`）只保留为诊断信息，不影响 W25Q256 兼容性判定。QE 已开启时不写 Flash；QE 为 0 时的 SR2 配置最多阻塞 20 ms，且仅发生在调度器启动前。`0xEC` 启动自检只读取物理地址 0 的 4 字节，不解释或记录内容。非阻塞路线只有在已设置上层回调、且无其他读取在飞时才允许启动：IRQ 先更新 Adapter 结果，再转发轻量事件；拥有请求的任务随后调用 `ProcessTransfer()`，成功时数据才可被 CPU 使用。当前唯一上层订阅者是 APP 的 `storage_flash` Module：它在 Storage Task 启动时建立订阅并长期持有，不由 benchmark 或其他业务 Module 临时设置或清除。`Platform_Flash_RunDiagnostic()` 使用调用者的 4 KiB 缓冲，顺序操作地址 `0x00000000` 与 `0x01FFF000` 两个保留扇区；它在显式板测中同步等待 Component 的异步擦写与轮询读回完成。成功后，拥有同一 QSPI/MDMA 订阅的任务可通过受限的区域语义分别启动两个 MDMA 读回，再由 Platform 比较私有图样；Platform 不等待任务通知、不访问 DMA 缓冲区。两条读回共同验证后，诊断入口仍不能被 FTL 或 MSC 当作通用写入能力。FTL 实现后才会增加 Flash FTL Handle 与 Bridge Bind。

## 运行时请求与约束

- 上层只使用 `Platform_Flash_*` 所表达的板级 Flash 能力，不访问 QSPI Handle、W25Qxx Handle 或未来 FTL 私有元数据；当前的 `ReadArray()` 是 FTL 接入前受限的物理读取接缝，不是逻辑地址 API；
- 非阻塞读取缓冲区必须可被 MDMA 访问，首地址和长度均按 32-byte Cache line 对齐；从 `StartReadArray()` 成功到 `ProcessTransfer()` 返回前，任何 CPU 或 DMA 都不得读取、写入或复用该区；
- 自检入口固定使用 ADR-0009 的两个 4 KiB 扇区，并依赖调用者在启动诊断窗口独占 Flash；正常功能、资源包、镜像槽和未来 FTL 都不得传入或复用这些物理范围；
- 本 Module 不实现 W25Q 指令、FTL 映射、FatFs 挂载、USB MSC 所有权、媒体扫描或任务策略；
- QSPI 实例、引脚、时钟和启动顺序是本 PCB 的事实，必须由 Platform 管理并与 CubeMX 配置同步核对。
