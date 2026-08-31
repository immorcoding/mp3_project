# W25Qxx STM32 HAL QSPI Adapter

本 Adapter 将 STM32H743 的 HAL QSPI 能力实现为 W25Qxx Component 所拥有的 `W25Qxx_BusOps`。无地址单线读供 JEDEC ID 与 SR1/SR2 使用，SFDP 使用 24-bit `1-1-1` 读取；W25Q256 原始读使用 `0xEC` 的固定 32-bit `1-4-4`，既保留同步轮询路线，也可经 CubeMX 绑定的 `QUADSPI_FIFO_TH → MDMA` 启动非阻塞接收；页编程使用 `0x34` 的固定 32-bit `1-1-4`，4 KiB 擦除使用 `0x21` 的固定 32-bit `1-1` 无数据命令。无地址控制和无地址单字节写分别供 `0x06`、`0x31` 的启动期 QE 配置使用。页编程和擦除提交后，Adapter 以 `HAL_QSPI_AutoPolling_IT()` 自动读取 `0x05` 并等待 WIP 清零。它还根据 Component 给出的数组读取协议调用 `HAL_QSPI_MemoryMapped()` 打开只读映射，并以 `HAL_QSPI_Abort()` 退出；映射与间接读写、自动轮询互斥，WIP 检查和恢复策略均由 Platform 所有。

## 预期公开 Interface

- `W25Qxx_QSPI_STM32HALAdapter_Bind()`；
- `W25Qxx_QSPI_STM32HALAdapter_EnableMemoryMappedMode()` / `DisableMemoryMappedMode()`：仅供 Platform 根据 Component 的数组读取协议进入或退出 H7 QSPI 映射；Adapter 只维护控制器模式，不检查 WIP，也不对外暴露映射地址；
- `W25Qxx_QSPI_STM32HALAdapter_NotifyReadComplete()` / `NotifyStatusMatch()` / `NotifyOperationError()`：仅由 Platform 经 QSPI IRQ Adapter 在 ISR 中更新当前 MDMA 读取或自动状态轮询结果；
- 仅供 `Platform/flash` 长期持有的 QSPI Adapter Context。

## 编译期依赖

- `Components/w25qxx` 的公开 Bus Ops、状态和类型；
- STM32 HAL QSPI、CubeMX 生成的 QSPI Handle 与必要的 Cache Interface。

## 运行时请求路径

W25Qxx Component 经已绑定的 Bus Ops 发起 JEDEC ID、SR1/SR2 无地址读取、SFDP 带地址读取和数组读写/擦除；本 Adapter 分别以 `HAL_QSPI_Command()` 后接 `HAL_QSPI_Receive()` 或 `HAL_QSPI_Transmit()` 完成同步间接传输，或仅提交无数据命令。非阻塞 `0xEC` 先以 `HAL_QSPI_Command()` 装载协议阶段，再以 `HAL_QSPI_Receive_DMA()` 把 QSPI FIFO 阈值请求交给 MDMA；它只保存 DMA 缓冲区元数据和 `BUSY` 状态。`STM32QSPIIRQ` 在 QSPI 读取完成、自动轮询状态匹配、错误或中止 IRQ 中调用对应 Notify，普通任务经 `W25Qxx_Process()` 查询结果：读取成功时 Adapter 执行严格对齐的 D-Cache Invalidate，然后清理元数据。开始前使用 Clean+Invalidate，防止脏 Cache line 在 MDMA 后回写覆盖新数据。`0xEC` 将模式字节 `0xFF` 映射为 HAL `AlternateBytes`（四线、8 bit），后续 4 个时钟映射为 `DummyCycles`；两者不能混淆。进入映射时，Platform 先从 Component 取得同一描述；Adapter 以 `HAL_QSPI_MemoryMapped()` 载入该命令，关闭 HAL timeout counter，使 `0x90000000` 的 CPU 读取按该协议持续取数。退出时以 `HAL_QSPI_Abort()` 解除控制器映射，不改变 NOR 内部操作。`0x21` 使用单线 32-bit 地址和 `QSPI_DATA_NONE`，只提交擦除命令。随后 Adapter 以一字节、单线 `0x05` 加 `HAL_QSPI_AutoPolling_IT()` 等待 `(SR1 & 0x01) == 0`，使用硬件自动停止；Status Match IRQ 只置结果，普通上下文才由 Component 完成状态转换。超出 Component 软件时限时，`HAL_QSPI_Abort()` 仅停止控制器轮询，不试图中止 NOR 已开始的写擦。QE 为 0 时，Adapter 以只有 Command 阶段的 `0x06` 和 `HAL_QSPI_Command()` 后接 `HAL_QSPI_Transmit()` 的 `0x31` 完成配置；`HAL_GetTick()` 为 Component 的 20 ms 启动期 QE 轮询、100 ms 数组读、5 ms 页编程和 500 ms 扇区擦除状态机提供时间源。

## 约束

- 不固定引用某个全局 QSPI Handle、GPIO 或时钟参数；这些均由 `Platform/flash` 注入；
- 不包含 FTL、FatFs、USB MSC、Service 或 APP；
- 不承担 W25Qxx 指令语义、FTL 映射或板级启动策略。
