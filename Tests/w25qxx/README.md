# W25Qxx Component 主机测试

该目录独立编译 `Components/w25qxx/w25qxx.c` 与一个 Fake Bus，不链接 STM32 HAL、FreeRTOS 或目标固件。

当前行为测试：W25Qxx Device 在已绑定总线后初始化，必须通过 `0x9F` 读取三字节 JEDEC ID；只有制造商和容量与实例期望值相同才进入 READY，`MemoryType` 不参与判定。测试覆盖合法的 `EF 70 19`，以及厂商或容量不匹配时的拒绝路径；成功后调用者可通过 `W25Qxx_GetJedecID()` 读取缓存结果。

```powershell
cmake -S Tests/w25qxx -B build/tests/w25qxx -G "MinGW Makefiles"
cmake --build build/tests/w25qxx
ctest --test-dir build/tests/w25qxx --output-on-failure
```

测试构建目录不属于目标固件 CMake 的源文件收集范围，也不应提交产物。
