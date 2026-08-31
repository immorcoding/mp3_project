# ADR-0001：W25Q256 的 Component、Adapter 与 Platform 接缝

- 状态：已接受
- 日期：2026-08-28
- 相关实现说明：[../w25q256_architecture.md](../w25q256_architecture.md)

## 背景

工程准备接入 W25Q256 外部 NOR Flash。它既需要 STM32H743 QSPI 后端，又需要在未来转换为可安全使用的逻辑扇区。现有 AXP2101 与 SoftI2C 已采用“Component 拥有 Ops、Bridge 连接 Component、Platform 负责装配”的模式。

## 决定

1. W25Qxx 原始 NOR 协议属于 `Components/w25qxx`；该 Component 拥有 `W25Qxx_BusOps`。
2. Flash Translation Layer 属于 `Components/flash_ftl`；该 Component 拥有 `FlashFTL_RawOps`。
3. `Adapters/stm32_hal/w25qxx_qspi` 实现 W25Qxx 的 Bus Ops，并集中所有 STM32 HAL QSPI 依赖。
4. `Adapters/bridge/flash_ftl_w25qxx` 以 W25Qxx 的公开 Interface 实现 Flash FTL 的 Raw Ops；该 Bridge 不依赖 HAL、CMSIS、FreeRTOS 或 Platform。
5. `Platform/flash` 长期持有两个 Component Handle 与 QSPI Adapter Context，完成 Bind、初始化和本 PCB 的 QSPI 资源装配。
6. 首个垂直切片实现 W25Qxx 的 QSPI 间接识别、读、页编程、扇区擦除和自动状态轮询，但不实现 Flash FTL、FatFs、USB MSC 或媒体业务。

## 不采用的方案

- **把 FTL 放入 Platform/flash**：会把可复用的逻辑扇区与擦写规则绑定为本 PCB 实现，降低测试与替换后端时的局部性。
- **让 Flash FTL 直接调用 W25Qxx**：会使 FTL 编译期耦合具体芯片 Device，失去替换原始 NOR 后端的接缝。
- **把 Component Bridge 放入 `stm32_hal/`**：Bridge 不依赖 HAL；放入该目录会混淆“跨 Component 语义转换”和“具体 MCU 后端实现”。
- **现在抽象通用 BlockDevice**：当前尚未有两个完成的逻辑块实现；过早抽象会制造没有真实消费者的浅 Module。

## 后果

- Flash FTL 可以在主机测试中绑定 Fake Raw Ops，不需要 QSPI 或 STM32 HAL；
- W25Qxx 可以单独绑定 HAL QSPI、其他 MCU Adapter 或测试 Adapter；
- 后续 FatFs/MSC 只能消费完成后的 FTL 逻辑扇区，不得绕过 FTL 访问原始 NOR；
- 每次开始实现本 ADR 的任一 Module 前，需补充对应 README、技术文档和测试策略。
