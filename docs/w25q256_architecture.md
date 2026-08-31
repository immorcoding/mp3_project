# W25Q256 外部 NOR Flash 架构

> 适用工程：`version0.4.0` 及后续版本  
> 状态：启动识别、QE 配置、`0xEC` 同步与 QSPI/MDMA 非阻塞 Quad I/O 读取、`0x34` 非阻塞页编程及 `0x21` 非阻塞 4 KiB 扇区擦除已实现；页编程和擦除完成由 QSPI 自动状态轮询的 Status Match IRQ 异步收尾。APP 可选轮询/MDMA 读取测速与首尾双自检扇区的轮询/MDMA 双读回校验已接入。W25Qxx 协议状态机已通过主机测试；120 MHz QSPI、双端 8-beat MDMA burst 下顺序读取已在板上达到约 50 MiB/s。自动状态轮询的新板级路径应以首尾 8 KiB 自检日志再次确认
> 当前范围：W25Qxx Device、STM32 HAL QSPI Adapter、Platform Flash 的最小识别、Quad 读写/擦除诊断路径，以及 Flash FTL 和跨 Component Bridge 的后续职责边界
> 相关 ADR：[ADR-0001：W25Q256 的 Component、Adapter 与 Platform 接缝](adr/0001-w25q256-component-seams.md)、[ADR-0009：W25Q256 固件双槽与自检区保留](adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md)

## 1. 当前范围与非目标

本轮当前实现 W25Qxx 的启动识别、一次性状态快照、QE 能力配置、固定 4-byte Quad I/O 的同步/MDMA 非阻塞读取、单页异步编程与 4 KiB 扇区异步擦除，以及写擦期间以 `0x05` 等待 WIP 清零的硬件自动状态轮询；Platform 只把后两项用于固定自检区，不实现以下内容：

- 内存映射或 FTL 算法；
- FatFs 内部卷、USB MSC、CDC + MSC Composite；
- Media Library、Queue、播放、歌曲元数据或 GUI 数据绑定。

W25Q256 是 NOR Flash，而不是 SD 卡。其页编程与擦除块规则不能直接暴露给 FatFs；原始芯片能力与未来逻辑扇区之间必须存在 Flash FTL。

### 1.1 后续内存映射的 WIP 契约

QSPI 内存映射尚未接入，但其与现有间接读写路径的互斥规则已经确定。`SR1.WIP` 的全称为 **Write In Progress**：它只表示 NOR 正在执行会改变非易失阵列的内部操作，例如页编程、擦除或状态寄存器写入。普通 `0xEC` 数组读取不会置位或清除 WIP；读取期间的 `W25Qxx` Device `BUSY`、STM32 QSPI 间接接收 `BUSY`、或 MDMA 传输中，均是控制器/软件的瞬态状态，不能与 Flash 的 SR1.WIP 混淆。

STM32 QSPI 的内存映射、间接读写和自动状态轮询是互斥功能模式。内存映射访问 `0x90000000` 窗口时，控制器只会按预配置读协议发起数组读取；它不会自动发送 `0x05`、检查 WIP 或等待 WIP 清零。因此 Resource Pack 等映射消费者不应在每次指针读取前查询 WIP；Platform 必须以模式切换保证整个窗口有效：仅在已确认 WIP=0 且没有在飞 Flash 操作时开启映射，擦写前退出映射，随后以现有 `0x05` 自动轮询等待 WIP=0，最后才恢复映射。在初始化/MCU 独立复位后的恢复路径同样必须先确认 Flash 空闲，不能假定映射读会自行等待先前残留的写擦。

## 2. 当前已实现的最小垂直切片

```text
app_init()
  -> Platform_Init()
  -> Platform_Flash_Init()
  -> W25Qxx_QSPI_STM32HALAdapter_Bind()
  -> W25Qxx_Init()
  -> 0x9F Read JEDEC ID
  -> W25Qxx_ProbeSFDP()
  -> 0x5A Read SFDP at 0x000000
  -> W25Qxx_EnsureQuadEnabled()
  -> read SR1/SR2; if QE=0: 0x06 -> verify WEL -> 0x31 -> poll WIP -> verify QE
  -> W25Qxx_Read(0x000000, 4)
  -> 0xEC Fast Read Quad I/O with 4-Byte Address
  -> STM32QSPIIRQ_Register(hqspi)
app_init()
  -> Platform_Flash_ReadStatusRegisters()
  -> 0x05 Read SR1
  -> 0x35 Read SR2
```

`W25Qxx_Init()` 使用单线 SDR 的无地址 `0x9F` 命令读取并缓存三个 ID 字节。W25Qxx Component 公开各容量代码和 `W25Qxx_ExpectedJedecIDTypeDef`，但不拥有当前 PCB 的型号选择；Platform Flash 注入本板的 Winbond `EF` 与 256 Mbit `19`，Component 只比较这两个字节。`MemoryType`（典型为 `40` 或 `70`）完整缓存为诊断快照，却不参与首版兼容性判定，因此 `EF 40 19` 与 `EF 70 19` 都可通过。随后 `W25Qxx_ProbeSFDP()` 通过 `0x5A` 从 24-bit 地址 `0x000000` 读取四字节 `SFDP` 签名，使用 8 个 dummy cycle，验证带地址的单线间接读取路径。SFDP 成功后，`W25Qxx_EnsureQuadEnabled()` 读取 SR1/SR2：QE 已为 1 时不写 Flash；QE 为 0 时仅在 WIP=0 下执行 `0x06`、确认 WEL、以 `0x31` 回写原始 `SR2 | QE`，随后按 W25Q256JV `tW` 最大 15 ms 加余量，以 20 ms 上限轮询 WIP，并回读确认 QE。初始 WIP=1 时拒绝改写。QE 成功后，Platform 从物理地址 `0x000000` 读取 4 字节验证 `0xEC` 通路：指令单线，32-bit 地址四线，随后以四线发送连续读取模式字节 `0xFF`，再使用 4 个 dummy clock 四线接收数据。模式字节和 dummy clock 是两个独立协议阶段，HAL 中分别映射为 `AlternateBytes` 和 `DummyCycles`。该启动读不解释数据且不改写 Flash。本板配置、SFDP 命令或签名、QE 配置或 `0xEC` 读取失败时，`Platform_Flash_Init()` 返回失败，当前启动策略将其视为致命硬件识别错误。成功时 APP 仅记录整机 `PLATFORM` 初始化完成；JEDEC 与 SR1/SR2 快照保留给按需诊断 API，不在常规启动日志逐项输出。启动期 QE 轮询不等同于后续擦写的自动状态轮询。

`W25Qxx_StartRead()` 与 `W25Qxx_Read()` 使用同一 `0xEC` 协议和边界检查。前者在 Adapter 接受 `HAL_QSPI_Receive_DMA()` 后把 Device 置为 BUSY；QSPI 完成、错误或中止 IRQ 只写 Adapter 的易失结果并向唯一订阅者发轻量通知。拥有请求的普通任务收到通知后调用 `Platform_Flash_ProcessOperation()`，其内部经 `W25Qxx_Process()` 查询 Adapter；成功路径在 Adapter 内失效严格对齐的 D-Cache 范围后，Device 才回到 READY，数组读超过 100 ms 则 ERROR。此路由不要求也不读取 NOR 的 WIP。`W25Qxx_ProgramPageStart()` 使用 `0x34`，因此同样固定为 32-bit 地址而不依赖全局 4-byte Address Mode；它发送单线地址和四线数据，只接受 `1..256` 字节且不得跨越物理 256-byte 页。`W25Qxx_SectorEraseStart()` 使用固定 4-byte、单线地址且无数据阶段的 `0x21`，只接受 4 KiB 对齐地址。两个启动函数都会先读取状态确认 WIP=0，发送 `0x06` 后回读 WEL；页编程另要求 QE=1。命令送达后，Component 调用 Bus Ops 启动 `0x05` 的一字节硬件自动轮询，以 `(SR1 & 0x01) == 0` 为匹配条件；`HAL_QSPI_AutoPolling_IT()` 的 Automatic Stop 使匹配时控制器停止，Status Match IRQ 只更新 Adapter 结果并唤醒唯一订阅者。`W25Qxx_Process()` 不再反复读取 SR1：它仅查询该结果，页编程超过 5 ms 或扇区擦除超过 500 ms 时以 `HAL_QSPI_Abort()` 停止控制器轮询、置 ERROR（不会中止 NOR 已开始的内部写擦）。它不阻塞等待 DMA、`tPP` 或 `tSE`，因此不能阻塞 MSC 或文件系统后台任务。该状态机已由 Fake Bus 覆盖；当前 Platform 仅能在两个预留自检扇区内经 Storage Task 等待破坏性操作完成，FTL 未来必须复用异步语义。

当前 CubeMX QSPI 使用 D1HCLK 240 MHz、prescaler 1，即 120 MHz；`ChipSelectHighTime = 6 cycles`，等于 50 ns，满足 W25 数据手册中擦除、编程/写事务最严格的 `/CS Deselect Time` 50 ns。`QUADSPI_IRQn` 与 `MDMA_IRQn` 均由 CubeMX 注册为优先级 5。MDMA Channel0 使用 `QUADSPI_FIFO_TH` 请求、字节宽度、源地址固定和目的地址递增；其 `BufferTransferLength` 必须与 QSPI `FifoThreshold` 保持相同值，且 Burst 长度不得超过该值。两项参数及 Source/Destination Burst 均属于 CubeMX 配置，`.ioc` 与重新生成的 `quadspi.c` 必须同步后才可作为板级事实记录；MDMA 只用于非阻塞 `0xEC` 接收，页编程和扇区擦除的数据/命令阶段保持 HAL 间接轮询传输，随后由 QSPI 的 `HAL_QSPI_AutoPolling_IT()` 轮询 WIP。

`Platform_Flash_ReadArray()` 是当前 FTL 接入前面向 APP 的同步原始数组读取能力：它仍完整保留 `0xEC` 的 4-byte 首地址对齐语义，不向 APP 暴露 W25Qxx 或 HAL Handle。另有成对的 `Platform_Flash_SetOperationCallback()`、`StartReadArray()`、`ProcessOperation()` 与 `ClearOperationCallback()` 提供单订阅者的异步路线。APP 的 `APP/tasks/storage/storage_flash.c` 在 Storage Task 启动时建立并长期持有该唯一订阅：IRQ 只写索引 2 任务通知，拥有请求的普通任务醒来后才调用 `ProcessOperation()` 收尾；在此之前不得访问或复用 MDMA 缓冲，也不能在 Status Match 后直接提交下一页。基准和未来 Flash 业务都不得临时抢占或清除该回调槽。`APP/tasks/storage/benchmark/storage_flash_benchmark.c` 当前由开关决定是否运行；启用后由 Storage Task 从 `0x00000000` 顺序读取 1 MiB、每次 4 KiB，先输出 `Bench poll read`，再经 `storage_flash` 以通知索引 2 输出 `Bench MDMA read`。两项都是端到端读取吞吐，不是四线 QSPI 的理论线速，也不修改 Flash 内容。

当 `STORAGE_FLASH_BENCHMARK_PROGRAM_ENABLE` 明确开启时，同一基准只分配一个 4 KiB、32-byte 对齐的 AXI SRAM 工作缓冲。它固定通过首/尾区域语义操作 `0x00000000` 与 `0x01FFF000` 两个 ADR-0009 保留扇区：每个扇区先由 `storage_flash_erase_diagnostic()` 提交 `0x21` 并等待 Status Match，再由 `Platform_Flash_FillDiagnosticBuffer()` 生成私有地址相关图样，最后经 `storage_flash_program_diagnostic_page()` 分 16 页提交 `0x34` 并逐页等待 Status Match。写入图样只执行一轮；同步读回成功后，Storage Task 保持同一 QSPI/MDMA 订阅，并通过 `Platform_Flash_StartDiagnosticRead()` 按首/尾区域语义分别读取两个扇区，在 `ProcessOperation()` 完成 Cache 收尾后由 `Platform_Flash_VerifyDiagnosticReadBuffer()` 对同一私有图样逐字节校验。轮询和 MDMA 两条读回只验证完整性；三个 Flash 吞吐日志统一由 DWT 周期计数和 `SystemCoreClock` 换算，避免 FreeRTOS Tick 的毫秒量化。成功时唯一的 `Self-test passed` 日志同时报告两扇区共 8 KiB 的逐页 `0x34` 端到端写速，计时包含每页提交、自动 WIP 轮询、任务通知与普通上下文收尾，不包含擦除、图样生成或读回。读取吞吐仍只由前置的独立 1 MiB 基准输出。失败不自动重试，避免重复磨损。它是启动早期独占的破坏性板测，不是 FTL、MSC 或普通写入 API。

## 3. 已接受的物理分区规划

以下规划针对 32 MiB W25Q256 的物理地址 `0x00000000–0x01FFFFFF`。它是 Bootloader、诊断、未来 Resource Pack 与 Flash FTL 共同遵守的分区契约；当前代码尚未实现镜像协议、资源包装载或 FTL。

| 物理范围 | 容量 | 所有者/用途 | FTL 是否可用 |
| --- | ---: | --- | --- |
| `0x00000000–0x00000FFF` | 4 KiB | 首自检扇区；仅显式读写诊断 | 否 |
| `0x00001000–0x00200FFF` | 2 MiB | `rollback` 固件镜像槽 | 否 |
| `0x00201000–0x00400FFF` | 2 MiB | `candidate` 固件镜像槽 | 否 |
| `0x00401000–0x01FFEFFF` | 27.99 MiB / 7166 个 4 KiB 扇区 | 尚未格式化的系统数据候选范围；先从其低地址划定容量待定的原始 Resource Pack，剩余部分才可交给 FTL | 待资源包边界确定后可用 |
| `0x01FFF000–0x01FFFFFF` | 4 KiB | 尾自检扇区；仅显式读写诊断 | 否 |

两个自检扇区只在相应诊断宏开启时擦除、页编程和读回；普通启动只能进行非破坏性读取检查。`rollback` 和 `candidate` 槽由未来 Bootloader 的镜像头、完整性/真实性校验、试运行确认和恢复状态机直接管理，不经过 FTL 或 FatFs。字库和模型将放入位于第四行低地址、连续且原始的 Resource Pack，通过 QSPI 内存映射读取；其精确容量、镜像头和 CRC 尚待首次资源包落盘前确定。Resource Pack 之后的连续剩余范围才可由 FTL 格式化，用于错误日志和可写文件；FTL 元数据、磨损均衡与掉电恢复仍待定义。

## 4. 目录、所有权与 Interface

```text
Components/
  w25qxx/                         W25Q 系列原始 NOR Device
  flash_ftl/                      逻辑扇区、映射和擦写管理

Adapters/
  stm32_hal/w25qxx_qspi/          STM32 HAL QSPI -> W25Qxx_BusOps
  bridge/flash_ftl_w25qxx/        W25Qxx Interface -> FlashFTL_RawOps

Platform/
  flash/                           当前 PCB 的 W25Q256/QSPI 对象装配
```

Interface 所有权遵循本工程统一规则：需要某项外部能力且经 Ops 发起调用的 Component，拥有该 Ops Interface。

| Component | 拥有的 Interface | 具体实现者 |
| --- | --- | --- |
| W25Qxx | `W25Qxx_BusOps` | `Adapters/stm32_hal/w25qxx_qspi` |
| Flash FTL | `FlashFTL_RawOps` | `Adapters/bridge/flash_ftl_w25qxx` |

因此 Bridge 不定义 FTL 所需的 Interface；它只实现并绑定 Flash FTL 已定义的 Raw Ops。Platform 也不实现 Ops，它只长期持有对象和 Context、执行 Bind，并维护本 PCB 的 QSPI 资源。

## 5. 三类关系

### 5.1 功能/抽象所有权

```text
STM32 HAL QSPI Adapter  ->  W25Qxx Device  ->  Flash FTL  ->  Platform Flash
```

该图只描述由低到高的语义提升，不表示头文件方向。

### 5.2 编译期依赖与装配

```text
W25Qxx owns W25Qxx_BusOps
        ^
STM32 HAL QSPI Adapter implements it

Flash FTL owns FlashFTL_RawOps
        ^
W25Qxx FTL Bridge implements it by using W25Qxx public Interface

Platform Flash includes both Components and both Adapters, then binds Handles + Context.
```

`Components/flash_ftl` 不包含 `Components/w25qxx` 或任何 Adapter；`Adapters/bridge/flash_ftl_w25qxx` 可以同时包含两个 Component 的公开头，但不得包含 HAL。只有 `Adapters/stm32_hal/w25qxx_qspi` 可以包含 QSPI HAL 和 CubeMX QSPI 类型。

### 5.3 运行时请求路径

```text
Flash FTL
  -> FlashFTL_RawOps
  -> Flash FTL W25Qxx Bridge
  -> W25Qxx public Interface
  -> W25Qxx_BusOps
  -> STM32 HAL QSPI Adapter
  -> STM32 HAL / QSPI peripheral
```

该路径不意味着 Flash FTL 在编译期依赖 W25Qxx；Bridge 是两者之间的接缝。

## 6. 为什么暂不直接接 FatFs

NOR Flash 必须先擦除再编程，擦除粒度也大于典型文件系统扇区。若直接把原始 W25Qxx 映射为 FatFs `disk_write`，更新一个逻辑扇区可能破坏同一擦除块中的相邻数据，并使高频改写区域过早损耗。

Flash FTL 需要先明确并验证：

- 逻辑扇区与物理页、擦除块的映射；
- 擦除、写入、同步与失败恢复语义；
- 元数据、备用块与掉电一致性策略；
- 对上层可见的逻辑容量。

在这些规则完成前，不新增 `BlockDevice` 抽象，也不让 `Service/filesystem`、FatFs 或 USB MSC 接触原始 W25Qxx。

## 7. 后续实施顺序

1. 在板上验证 JEDEC ID 和 SFDP 签名，确认 QSPI 引脚、时钟、片选时序、无地址与带地址的单线读路径；
2. 在板上回归 QE 的“已开启零写入”、`0xEC` 读取路径及其 APP 顺序读取吞吐；在两个自检扇区验证擦除、`0x34` 页编程、轮询读回与 MDMA 读回图样校验；
3. 实现并验证 QSPI 内存映射：只在 WIP=0 时打开窗口；写擦前退出映射，Status Match 后恢复映射，并验证字库/模型的只读访问与 Cache 一致性；
4. 定义 Bootloader 镜像头、候选/回滚状态、完整性/真实性校验与断电恢复协议，再实现镜像槽的读写流程；
5. 在首次写入资源前，确定原始 Resource Pack 的容量、镜像头、CRC 与固定边界；以已验证的 QSPI 内存映射读取字库/模型，且不让其进入 FTL；
6. 设计并实现 Flash FTL，再以独立测试验证逻辑扇区、保留范围排除与掉电恢复边界，并讨论是否接入 `Service/filesystem`、FatFs 与 USB MSC 所有权切换。

Media Library、Queue 与播放读取属于第 5 步之后的上层消费者，不属于本架构文档的当前实现范围。
