# W25Qxx Component 主机测试

该目录独立编译 `Components/w25qxx/w25qxx.c`、`Components/flash_ftl/flash_ftl.c`、`Adapters/bridge/flash_ftl_w25qxx/flash_ftl_w25qxx_bridge.c` 与 Fake Bus，不链接 STM32 HAL、FreeRTOS 或目标固件。运行方法见 [主机回归操作手册](../README.md)。

## 范围

- 初始化时仅执行 `0x9F` JEDEC ID 校验；初始化之外由 `W25Qxx_ProbeSFDP` 独立执行 `0x5A` SFDP 读取与签名校验；`0x05`/`0x35` 状态寄存器读取与 WIP/WEL/QE 解析为独立的状态寄存器读取/解析。
- QE 配置状态机：`0x06`、WEL 核验、`0x31`、WIP 清零与回读；已开启直通及忙时拒绝。
- W25Q256 `0xEC` 固定四线数组读取协议、异步读取状态与超时，以及 `0x34` 页编程、`0x21` 扇区擦除的地址、对齐、轮询和超时边界。
- **读墙钟与读完成状态的顺序**（#220）：Fake 的 `GetTickMs` 在被置位后的下一次调用里模拟“任务在读状态与读墙钟之间被抢占”——墙钟前进到（或越过）时限、锁存状态同时变成已完成。页编程、扇区擦除、异步数组读取三条路径各一例：被抢占越过时限但芯片已完成，必须按完成处理，不中止轮询、不进 ERROR（旧写法先读状态再读墙钟会在这里误判超时）；真的一直忙，被抢占越过时限仍按各自的超时报错并中止轮询；时限边界不动（页编程差 1 ms 仍是忙，到 5 ms 才超时）。保真边界：证明 Component 内两次读取的先后顺序，不证明真实 HAL_GetTick 与 QSPI 中断的时序。
- Flash FTL 与 W25Qxx Bridge：分区范围、芯片容量越界、分区末端跨界及极大非法偏移拒绝、FTL 地址到芯片地址转换，以及 Bridge 实现 FTL 拥有的 `FlashFTL_RawOps` 接缝 `ReadStart`/`Process` 异步受理、忙态与完成推进。
- H7 内存映射属于 Adapter/Platform 板级集成，不在 Fake Bus 主机测试范围内。
