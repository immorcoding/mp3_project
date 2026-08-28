# W25Q256 外部 NOR Flash 架构

> 适用工程：`version0.4.0` 及后续版本  
> 状态：目录与架构接缝已确定，尚未开始实现或配置 QSPI  
> 当前范围：W25Qxx Device、STM32 HAL QSPI Adapter、Flash FTL 和跨 Component Bridge 的职责边界
> 相关 ADR：[ADR-0001：W25Q256 的 Component、Adapter 与 Platform 接缝](adr/0001-w25q256-component-seams.md)

## 1. 当前范围与非目标

本轮只为 W25Q256 建立底层存储架构，不实现以下内容：

- QSPI CubeMX 配置、芯片驱动、擦写测试或 FTL 算法；
- FatFs 内部卷、USB MSC、CDC + MSC Composite；
- Media Library、Queue、播放、歌曲元数据或 GUI 数据绑定。

W25Q256 是 NOR Flash，而不是 SD 卡。其页编程与擦除块规则不能直接暴露给 FatFs；原始芯片能力与未来逻辑扇区之间必须存在 Flash FTL。

## 2. 目录、所有权与 Interface

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

## 3. 三类关系

### 3.1 功能/抽象所有权

```text
STM32 HAL QSPI Adapter  ->  W25Qxx Device  ->  Flash FTL  ->  Platform Flash
```

该图只描述由低到高的语义提升，不表示头文件方向。

### 3.2 编译期依赖与装配

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

### 3.3 运行时请求路径

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

## 4. 为什么暂不直接接 FatFs

NOR Flash 必须先擦除再编程，擦除粒度也大于典型文件系统扇区。若直接把原始 W25Qxx 映射为 FatFs `disk_write`，更新一个逻辑扇区可能破坏同一擦除块中的相邻数据，并使高频改写区域过早损耗。

Flash FTL 需要先明确并验证：

- 逻辑扇区与物理页、擦除块的映射；
- 擦除、写入、同步与失败恢复语义；
- 元数据、备用块与掉电一致性策略；
- 对上层可见的逻辑容量。

在这些规则完成前，不新增 `BlockDevice` 抽象，也不让 `Service/filesystem`、FatFs 或 USB MSC 接触原始 W25Qxx。

## 5. 后续实施顺序

1. 在 CubeMX 中配置实际 QSPI 外设、引脚、时钟和必要中断；
2. 实现 W25Qxx Device 与 STM32 HAL QSPI Adapter，验证 JEDEC ID、读取、页编程、擦除和忙状态；
3. 设计并实现 Flash FTL，再以独立测试验证逻辑扇区与掉电恢复边界；
4. FTL 稳定后，再讨论是否接入 `Service/filesystem`、FatFs 与 USB MSC 所有权切换。

Media Library、Queue 与播放读取属于第 4 步之后的上层消费者，不属于本架构文档的当前实现范围。
