# W25Q256 外部 NOR Flash 架构

> 适用工程：`version0.4.0` 及后续版本  
> 状态：启动识别与 QE 配置垂直切片已实现；间接模式 JEDEC ID、SFDP、SR1/SR2 与 QE 配置已通过主机测试，等待板上回归
> 当前范围：W25Qxx Device、STM32 HAL QSPI Adapter、Platform Flash 的最小识别与 Quad 能力配置路径，以及 Flash FTL 和跨 Component Bridge 的后续职责边界
> 相关 ADR：[ADR-0001：W25Q256 的 Component、Adapter 与 Platform 接缝](adr/0001-w25q256-component-seams.md)

## 1. 当前范围与非目标

本轮当前实现 W25Qxx 的启动识别、一次性状态快照与 QE 能力配置，不实现以下内容：

- 页编程、擦除、自动状态轮询、4-byte 地址切换、DMA、内存映射或 FTL 算法；
- FatFs 内部卷、USB MSC、CDC + MSC Composite；
- Media Library、Queue、播放、歌曲元数据或 GUI 数据绑定。

W25Q256 是 NOR Flash，而不是 SD 卡。其页编程与擦除块规则不能直接暴露给 FatFs；原始芯片能力与未来逻辑扇区之间必须存在 Flash FTL。

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
app_init()
  -> Platform_Flash_ReadStatusRegisters()
  -> 0x05 Read SR1
  -> 0x35 Read SR2
```

`W25Qxx_Init()` 使用单线 SDR 的无地址 `0x9F` 命令读取并缓存三个 ID 字节。W25Qxx Component 公开各容量代码和 `W25Qxx_ExpectedJedecIDTypeDef`，但不拥有当前 PCB 的型号选择；Platform Flash 注入本板的 Winbond `EF` 与 256 Mbit `19`，Component 只比较这两个字节。`MemoryType`（典型为 `40` 或 `70`）完整缓存并写入启动日志，却不参与首版兼容性判定，因此 `EF 40 19` 与 `EF 70 19` 都可通过。随后 `W25Qxx_ProbeSFDP()` 通过 `0x5A` 从 24-bit 地址 `0x000000` 读取四字节 `SFDP` 签名，使用 8 个 dummy cycle，验证带地址的单线间接读取路径。SFDP 成功后，`W25Qxx_EnsureQuadEnabled()` 读取 SR1/SR2：QE 已为 1 时不写 Flash；QE 为 0 时仅在 WIP=0 下执行 `0x06`、确认 WEL、以 `0x31` 回写原始 `SR2 | QE`，随后按 W25Q256JV `tW` 最大 15 ms 加余量，以 20 ms 上限轮询 WIP，并回读确认 QE。初始 WIP=1 时拒绝改写。该阻塞轮询只位于调度器前的启动能力配置，不能作为未来页编程、擦除、USB MSC 或文件系统写路径的模型。本板配置、SFDP 命令或签名不符，或 QE 配置失败时，`Platform_Flash_Init()` 返回失败，当前启动策略将其视为致命硬件识别错误。`app_init()` 随后经 `Platform_Flash_ReadStatusRegisters()` 以 `0x05`、`0x35` 再读取 SR1/SR2，并记录原始值与 `WIP`、`WEL`、`QE` 位；这只是同步瞬态快照，当前不构成后续擦写的自动轮询或状态机。

当前 CubeMX QSPI 使用 D1HCLK 240 MHz、prescaler 5，即 40 MHz；`ChipSelectHighTime = 3 cycles`，等于 75 ns，满足 W25 数据手册中读、写、擦除事务最严格的 `/CS Deselect Time` 50 ns。`QUADSPI_IRQn` 已由 CubeMX 注册为优先级 5，但本阶段的阻塞式 `HAL_QSPI_Command()` / `HAL_QSPI_Receive()` 不依赖该 IRQ。

## 3. 目录、所有权与 Interface

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

## 4. 三类关系

### 4.1 功能/抽象所有权

```text
STM32 HAL QSPI Adapter  ->  W25Qxx Device  ->  Flash FTL  ->  Platform Flash
```

该图只描述由低到高的语义提升，不表示头文件方向。

### 4.2 编译期依赖与装配

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

### 4.3 运行时请求路径

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

## 5. 为什么暂不直接接 FatFs

NOR Flash 必须先擦除再编程，擦除粒度也大于典型文件系统扇区。若直接把原始 W25Qxx 映射为 FatFs `disk_write`，更新一个逻辑扇区可能破坏同一擦除块中的相邻数据，并使高频改写区域过早损耗。

Flash FTL 需要先明确并验证：

- 逻辑扇区与物理页、擦除块的映射；
- 擦除、写入、同步与失败恢复语义；
- 元数据、备用块与掉电一致性策略；
- 对上层可见的逻辑容量。

在这些规则完成前，不新增 `BlockDevice` 抽象，也不让 `Service/filesystem`、FatFs 或 USB MSC 接触原始 W25Qxx。

## 6. 后续实施顺序

1. 在板上验证 JEDEC ID 和 SFDP 签名，确认 QSPI 引脚、时钟、片选时序、无地址与带地址的单线读路径；
2. 在板上回归 QE 的“已开启零写入”路径；随后按独立测试与板上验证逐步实现 4-byte 地址原始读取、页编程、擦除和自动状态轮询；
3. 设计并实现 Flash FTL，再以独立测试验证逻辑扇区与掉电恢复边界；
4. FTL 稳定后，再讨论是否接入 `Service/filesystem`、FatFs 与 USB MSC 所有权切换。

Media Library、Queue 与播放读取属于第 4 步之后的上层消费者，不属于本架构文档的当前实现范围。
