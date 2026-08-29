# W25Qxx STM32 HAL QSPI Adapter

本 Adapter 将 STM32H743 的 HAL QSPI 能力实现为 W25Qxx Component 所拥有的 `W25Qxx_BusOps`。当前已实现同步、间接模式的无地址单线读命令，供 W25Qxx 初始化读取 JEDEC ID；DMA、自动状态轮询、内存映射和 QSPI IRQ 回调尚未接入。

## 预期公开 Interface

- `W25Qxx_QSPI_STM32HALAdapter_Bind()`；
- 仅供 `Platform/flash` 长期持有的 QSPI Adapter Context。

## 编译期依赖

- `Components/w25qxx` 的公开 Bus Ops、状态和类型；
- STM32 HAL QSPI、CubeMX 生成的 QSPI Handle 与必要的 Cache Interface。

## 运行时请求路径

W25Qxx Component 经已绑定的 Bus Ops 发起 JEDEC ID 无地址命令读取；本 Adapter 以 `HAL_QSPI_Command()` 后接 `HAL_QSPI_Receive()` 完成同步间接事务。后续使用 DMA 时，DMA 与 Cache 一致性仅在本 Adapter 的实现内收敛，完成事件仍通过已经注册的回调向上发布。

## 约束

- 不固定引用某个全局 QSPI Handle、GPIO 或时钟参数；这些均由 `Platform/flash` 注入；
- 不包含 FTL、FatFs、USB MSC、Service 或 APP；
- 不承担 W25Qxx 指令语义、FTL 映射或板级启动策略。
