# W25Qxx Component 主机测试

该目录独立编译 `Components/w25qxx/w25qxx.c` 与一个 Fake Bus，不链接 STM32 HAL、FreeRTOS 或目标固件。

当前行为测试：W25Qxx Device 在已绑定总线后初始化，必须通过 `0x9F` 读取三字节 JEDEC ID；只有制造商和容量与实例期望值相同才进入 READY，`MemoryType` 不参与判定。测试覆盖合法的 `EF 70 19`，以及厂商或容量不匹配时的拒绝路径；随后覆盖 `0x5A`、地址 0、24-bit 地址长度、8 个 dummy cycle 的 SFDP 读取，并分别验证正确与错误签名。状态寄存器测试覆盖 `0x05`、`0x35` 的 SR1/SR2 读取、WIP/WEL/QE 解析，以及 SR2 总线失败时拒绝返回不完整结果。QE 测试覆盖“已开启时零写入直通”“QE=0 时 `0x06 → WEL 核验 → 0x31 → WIP 清零 → QE 回读`”及“初始 WIP=1 时拒绝改写”三条路径。成功后调用者可通过 `W25Qxx_GetJedecID()` 读取缓存结果。

```powershell
cmake -S Tests/w25qxx -B build/tests/w25qxx -G "MinGW Makefiles"
cmake --build build/tests/w25qxx
ctest --test-dir build/tests/w25qxx --output-on-failure
```

测试构建目录不属于目标固件 CMake 的源文件收集范围，也不应提交产物。
