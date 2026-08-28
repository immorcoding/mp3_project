# W25Qxx Component

本 Module 预留给 W25Q 系列串行 NOR Flash 的可复用芯片协议与状态机。当前仅建立目录和架构约束，尚未创建源代码，也尚未接入 QSPI 外设。

## 预期公开 Interface

- `W25Qxx_*` Device Interface：芯片识别、读取、页编程、擦除和就绪状态；
- 由本 Component 拥有的 `W25Qxx_BusOps`：表达芯片协议所需的底层串行总线能力。

## 编译期依赖

- ISO C 与本 Module 的私有配置；
- 注入的 `W25Qxx_BusOps` 与配套 Context。

本 Component 不包含 STM32 HAL、CubeMX QSPI Handle、FTL、FatFs、USB MSC、FreeRTOS 或 Platform 头文件。

## 运行时请求路径

W25Qxx Device 经自身拥有的 Bus Ops 发起原始 NOR 读、页编程和擦除请求；`Adapters/stm32_hal/w25qxx_qspi` 将来实现这组 Ops。Flash FTL 若需要原始存储能力，只能经 `Adapters/bridge/flash_ftl_w25qxx` 调用本 Component 的公开 Interface。

## 生命周期与约束

- W25Qxx Handle 和 QSPI Adapter Context 由 `Platform/flash` 长期持有并完成绑定；
- 本 Module 只表达芯片协议与归一化设备状态，不拥有本 PCB 的 QSPI 实例、引脚、时钟参数或启动策略；
- 不直接实现逻辑扇区、磨损均衡、FatFs 卷、USB MSC 或媒体业务。
