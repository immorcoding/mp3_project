# W25Qxx Component 主机测试

该目录独立编译 `Components/w25qxx/w25qxx.c` 与一个 Fake Bus，不链接 STM32 HAL、FreeRTOS 或目标固件。

当前行为测试：W25Qxx Device 在已绑定总线后初始化，必须通过 `0x9F` 读取三字节 JEDEC ID；只有制造商和容量与实例期望值相同才进入 READY，`MemoryType` 不参与判定。测试覆盖合法的 `EF 70 19`，以及厂商或容量不匹配时的拒绝路径；随后覆盖 `0x5A`、地址 0、24-bit 地址长度、8 个 dummy cycle 的 SFDP 读取，并分别验证正确与错误签名。状态寄存器测试覆盖 `0x05`、`0x35` 的 SR1/SR2 读取、WIP/WEL/QE 解析，以及 SR2 总线失败时拒绝返回不完整结果。QE 测试覆盖“已开启时零写入直通”“QE=0 时 `0x06 → WEL 核验 → 0x31 → WIP 清零 → QE 回读`”及“初始 WIP=1 时拒绝改写”。数组读取测试核验 W25Q256 `0xEC` 的 32-bit 四线地址、四线 `0xFF` 模式字节和 4 个 dummy clock，并拒绝非 4-byte 对齐首地址或非 W25Q256 容量；异步数组读取还核验 `W25Qxx_StartRead()` 进入 `BUSY`、Adapter 报告完成后 `Process()` 回到 `READY`，以及 100 ms 未完成时进入 ERROR。页编程测试核验 `0x34` 的 32-bit 单线地址/四线数据、`0x06 → WEL` 前置条件、非阻塞 `WIP` 轮询、跨页和 QE 未开启拒绝，以及 5 ms 超时进入 ERROR。扇区擦除测试核验 `0x21` 的 32-bit 单线地址/无数据阶段、`0x06 → WEL` 前置条件、4 KiB 对齐拒绝、非阻塞轮询与 500 ms 超时进入 ERROR。成功后调用者可通过 `W25Qxx_GetJedecID()` 读取缓存结果。

```powershell
cmake -S Tests/w25qxx -B build/tests/w25qxx -G "MinGW Makefiles"
cmake --build build/tests/w25qxx
ctest --test-dir build/tests/w25qxx -C Debug --output-on-failure
```

测试构建目录不属于目标固件 CMake 的源文件收集范围，也不应提交产物。
