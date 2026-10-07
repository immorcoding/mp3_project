# 存储介质事实

STOR-2/3/10 的硬件来源材料。分区决定见 [ADR-0009](../../adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md)，Cache依据见 [ADR-0006](../../adr/0006-cache-range-ownership.md)，设备接口见 [W25Qxx README](../../../Components/w25qxx/README.md) 与 [SD README](../../../Components/sd/README.md)。

| W25Q256物理范围，含端点 | 保留用途 |
| --- | --- |
| 0x00000000–0x00000FFF | 首自检区 |
| 0x00001000–0x00200FFF | rollback固件槽 |
| 0x00201000–0x00400FFF | candidate固件槽 |
| 0x00401000–FTL基址前一字节 | 原始连续Resource Pack |
| FTL基址–0x01FFEFFF | FTL物理分区；基址=0x01FFF000−配置容量，默认24 MiB时为0x007FF000 |
| 0x01FFF000–0x01FFFFFF | 尾自检区 |

固件槽与原始资源区不经FTL/FatFs。FTL采用分区相对偏移，Bridge转芯片绝对地址。映射0x90000000–0x91FFFFFF对应物理32 MiB，只读且与间接/自动轮询模式互斥；映射访问不会自动读取WIP或等待写擦。

| 芯片契约 | 事实 |
| --- | --- |
| 识别 | 0x9F读JEDEC；本板比较Winbond EF与容量19，MemoryType仅诊断，EF4019/EF7019均可；0x5A以24-bit地址0、8 dummy读SFDP签名，只验证此路径 |
| QE | 已置位不写；WIP=0时0x06→验证WEL→0x31保留SR2其余位并置QE→等WIP清零→回读QE；初始忙拒绝修改 |
| 读 | 固定0xEC四字节地址、1-4-4，独立四线模式字节0xFF与4 dummy clocks；原始入口保持四字节首地址对齐 |
| 编程 | 0x34四字节单线地址、四线数据，1..256 B且不跨页；需WIP=0、QE与WEL |
| 擦除 | 0x21四字节单线地址、无数据，4 KiB对齐、确认空闲与WEL |
| 写擦完成 | 0x05自动轮询WIP=0并Automatic Stop；Status Match只通知任务收尾，启动QE软件轮询是另一条路径 |
| 忙与中止 | 普通读不改变WIP；软件/控制器/DMA BUSY不等于WIP；Abort不能撤销阵列内部写擦，MCU独立复位不证明NOR空闲 |
| H7传输 | MDMA BufferTransferLength等于QSPI FifoThreshold、burst不超过它；CS高电平满足最严格写事务50 ns；当前MDMA仅接收，编程数据仍轮询发送 |
| SD完成 | 数据DMA完成后卡仍可能在编程，需回到TRANSFER才接下一命令；无卡不是初始化致命失败 |
| Cache | H7专用缓冲独占32 B line且DMA可达；发送前Clean，接收前Clean+Invalidate、完成后Invalidate；FatFs任意指针不保证两项前提 |

可移除SD使用可返回错误的HAL初始化，CubeMX void入口的Error_Handler策略不适用于无卡。Adapter复制的初始化选项与生成的NVIC/回调配置必须同步核对。独立启动诊断的两路读回与测量范围查 [benchmark README](../../../APP/tasks/storage/benchmark/README.md)，不在此维护性能日志或调用流水。
