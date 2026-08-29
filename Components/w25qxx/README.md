# W25Qxx Component

本 Module 是 W25Q 系列串行 NOR Flash 的可复用芯片协议与状态机。当前已实现阻塞式 JEDEC ID 识别与实例注入式兼容性校验；页编程、擦除、状态轮询、4-byte 地址和内存映射仍未实现。

## 预期公开 Interface

- `W25Qxx_Init()`：读取并缓存三字节 JEDEC ID，并比较实例注入的制造商、容量期望值；
- `W25Qxx_GetJedecID()`：读取初始化成功后缓存的 JEDEC ID，不重新访问总线；
- 由本 Component 拥有的 `W25Qxx_BusOps`：当前提供无地址命令读取，供 JEDEC ID 使用。

公开头文件集中提供 W25Q 常用容量的 `W25QXX_CAPACITY_ID_*MBIT` 常量。Platform 选择当前 PCB 的厂商和容量组合后，经 `W25Qxx_ExpectedJedecIDTypeDef` 注入 Handle；`MemoryType` 不作为兼容性条件，因此同容量的 `EF 40 19` 与 `EF 70 19` 都可表示当前 W25Q256。

## 编译期依赖

- ISO C 与本 Module 的私有配置；
- 注入的 `W25Qxx_BusOps` 与配套 Context。

本 Component 不包含 STM32 HAL、CubeMX QSPI Handle、FTL、FatFs、USB MSC、FreeRTOS 或 Platform 头文件。

## 运行时请求路径

W25Qxx Device 经自身拥有的 Bus Ops 发起 JEDEC ID 读取；`Adapters/stm32_hal/w25qxx_qspi` 将实现这组 Ops。Flash FTL 若需要原始存储能力，只能经 `Adapters/bridge/flash_ftl_w25qxx` 调用本 Component 的公开 Interface。

## 生命周期与约束

- W25Qxx Handle、其 `ExpectedJedecID` 和 QSPI Adapter Context 均由 `Platform/flash` 长期持有并完成装配；
- 本 Module 只表达芯片协议与归一化设备状态，不拥有本 PCB 的 QSPI 实例、引脚、时钟参数或启动策略；
- 不直接实现逻辑扇区、磨损均衡、FatFs 卷、USB MSC 或媒体业务。
