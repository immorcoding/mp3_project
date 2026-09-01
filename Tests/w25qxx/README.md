# W25Qxx Component 主机测试

该目录独立编译 `Components/w25qxx/w25qxx.c`、`Components/flash_ftl/flash_ftl.c`、`Adapters/bridge/flash_ftl_w25qxx/flash_ftl_w25qxx_bridge.c` 与 Fake Bus，不链接 STM32 HAL、FreeRTOS 或目标固件。运行方法见 [主机回归操作手册](../README.md)。

## 范围

- 初始化时仅执行 `0x9F` JEDEC ID 校验；初始化之外由 `W25Qxx_ProbeSFDP` 独立执行 `0x5A` SFDP 读取与签名校验；`0x05`/`0x35` 状态寄存器读取与 WIP/WEL/QE 解析为独立的状态寄存器读取/解析。
- QE 配置状态机：`0x06`、WEL 核验、`0x31`、WIP 清零与回读；已开启直通及忙时拒绝。
- W25Q256 `0xEC` 固定四线数组读取协议、异步读取状态与超时，以及 `0x34` 页编程、`0x21` 扇区擦除的地址、对齐、轮询和超时边界。
- Flash FTL 与 W25Qxx Bridge：分区范围、芯片容量越界、分区末端跨界及极大非法偏移拒绝、FTL 地址到芯片地址转换，以及 Bridge 实现 FTL 拥有的 `FlashFTL_RawOps` 接缝 `ReadStart`/`Process` 异步受理、忙态与完成推进。
- H7 内存映射属于 Adapter/Platform 板级集成，不在 Fake Bus 主机测试范围内。
