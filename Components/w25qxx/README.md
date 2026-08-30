# W25Qxx Component

本 Module 是 W25Q 系列串行 NOR Flash 的可复用芯片协议与状态机。当前已实现阻塞式 JEDEC ID 识别、实例注入式兼容性校验、SFDP 签名探测、SR1/SR2 的同步读取与 WIP/WEL/QE 位解析、启动期 QE 的安全置位与回读核验，以及 W25Q256 的固定 4-byte Quad I/O 原始读取、非阻塞 Quad 页编程和 4 KiB Sector Erase 的启动/轮询；DMA、自动状态轮询、内存映射和 FTL 仍未实现。

## 预期公开 Interface

- `W25Qxx_Init()`：读取并缓存三字节 JEDEC ID，并比较实例注入的制造商、容量期望值；
- `W25Qxx_GetJedecID()`：读取初始化成功后缓存的 JEDEC ID，不重新访问总线；
- `W25Qxx_ProbeSFDP()`：以 `0x5A` 读取地址 0 的 SFDP 签名，验证带地址的间接读取路径；
- `W25Qxx_ReadStatusRegisters()`：以 `0x05`、`0x35` 读取 SR1、SR2，返回原始值及 WIP/WEL/QE 解析结果；
- `W25Qxx_EnsureQuadEnabled()`：启动期先读 QE；已开启时不改写，未开启时依次执行 `0x06`、WEL 回读、`0x31` 写 SR2、WIP 有界轮询和 QE 回读核验；
- `W25Qxx_Read()`：仅对当前 W25Q256 以 `0xEC` 执行固定 4-byte、`1-4-4` Quad I/O 原始读取。起始地址必须 4-byte 对齐；该限制来自 W25Q256 的 `0xEC` 命令，而非 QSPI 外设限制；
- `W25Qxx_ProgramPageStart()`：仅对当前 W25Q256 以 `0x34` 启动固定 4-byte、`1-1-4` Quad 页编程；仅接受未跨页的 `1..256` 字节请求，成功时只表示命令数据已送入 Flash；
- `W25Qxx_SectorEraseStart()`：仅对当前 W25Q256 以 `0x21` 启动固定 4-byte、`1-1` 的 4 KiB 扇区擦除；地址必须按 4 KiB 对齐，成功时只表示擦除命令已送入 Flash；
- `W25Qxx_Process()`：对已启动页编程或扇区擦除读取一次状态快照，返回 `BUSY`、完成的 `OK`，或页编程 5 ms / 扇区擦除 500 ms 的有界超时；不循环等待 `tPP` 或 `tSE`；
- 由本 Component 拥有的 `W25Qxx_BusOps`：提供无地址读/写/控制、带地址的无数据控制、可配置地址/交替字节/dummy/线数的带地址读写，以及毫秒时间源；Component 决定命令语义，Adapter 只映射事务。

公开头文件集中提供 W25Q 常用容量的 `W25QXX_CAPACITY_ID_*MBIT` 常量，供 JEDEC 识别和 Platform 选择使用。当前固定四字节数组 API 只支持容量码 `19` 的 W25Q256；若 Platform 以后选择较小容量，初始化可正常识别，但必须先增加与该器件相符的三字节读写命令描述，不能复用 `0xEC/0x34`。`MemoryType` 不作为兼容性条件，因此同容量的 `EF 40 19` 与 `EF 70 19` 都可表示当前 W25Q256。

## 编译期依赖

- ISO C 与本 Module 的私有配置；
- 注入的 `W25Qxx_BusOps` 与配套 Context。

本 Component 不包含 STM32 HAL、CubeMX QSPI Handle、FTL、FatFs、USB MSC、FreeRTOS 或 Platform 头文件。

## 运行时请求路径

W25Qxx Device 经自身拥有的 Bus Ops 发起 JEDEC ID、SR1/SR2、SFDP 和数组读写/擦除事务。启动期 QE 为 0 时，Component 在确认 WIP=0 后发送 `0x06`、回读 WEL、以 `0x31` 写入保留原值的 `SR2 | QE`，并在 20 ms 内轮询 WIP 再核验 QE；`Adapters/stm32_hal/w25qxx_qspi` 实现这组 Ops。`0xEC` 读取在 32-bit 四线地址之后以四线发送模式字节 `0xFF`，再发送 4 个 dummy clock；`0x34` 页编程以 32-bit 单线地址和四线数据输入传输，`0x21` 以 32-bit 单线地址发起 4 KiB 扇区擦除。两个异步操作均由调用者周期性调用 `W25Qxx_Process()` 推进；该函数一次只取一份状态快照，绝不阻塞等待 `tPP` 或 `tSE`。目标字节是否已擦除、任意长度拆页、掉电一致性与逻辑地址映射仍属于后续 FTL。Flash FTL 若需要原始存储能力，只能经 `Adapters/bridge/flash_ftl_w25qxx` 调用本 Component 的公开 Interface。

## 生命周期与约束

- W25Qxx Handle、其 `ExpectedJedecID` 和 QSPI Adapter Context 均由 `Platform/flash` 长期持有并完成装配；
- 本 Module 只表达芯片协议与归一化设备状态，不拥有本 PCB 的 QSPI 实例、引脚、时钟参数或启动策略；
- `0x34` 与 `0x21` 均在主机 Fake Bus 中验证了命令、WEL 前置条件和异步状态机，但尚未完成板上破坏性验证；只有 Platform Flash 的 ADR-0009 保留首尾自检扇区可调用它们，不能把该能力误用于普通数组；
- 不直接实现逻辑扇区、磨损均衡、FatFs 卷、USB MSC 或媒体业务。
