# FTL 主机回归

测试仅操作进程内 Fake NOR，不访问板卡。通过公开 FTL/RawOps 接口验证行为，真实 FatFs 集成在 BSP 契约处替换硬件与 RTOS 环境。

## 运行

在工程根目录，使用主机 GCC（不能使用 ARM 工具链）：

```powershell
cmake -S Tests/flash_ftl -B build/host-ftl -G Ninja -DCMAKE_C_COMPILER=E:/mingw64/bin/gcc.exe
cmake --build build/host-ftl
ctest --test-dir build/host-ftl --output-on-failure

cmake -S Tests/w25qxx -B build/host-w25qxx -G Ninja -DCMAKE_C_COMPILER=E:/mingw64/bin/gcc.exe
cmake --build build/host-w25qxx
ctest --test-dir build/host-w25qxx --output-on-failure
```

主机编译器路径按本机调整。测试显式保留 assert，不受 Release 的 NDEBUG 影响。

## 范围

- FTL 基本读写、部分/跨组更新、首尾和溢出边界、忙时拒绝、未映射 0xFF、持久化后重建 RAM。
- 页编程与格式化各写擦阶段撕裂；无有效提交不可见，最新已提交数据损坏报错。
- 持续覆盖触发前台 GC；维护最多一块并在高水位停止；旧块 GC 撕裂不误报卷损坏；虚假卷头擦除成功被全块验证发现。
- 全 FF 数据页跳过编程；异常后 Quiesce 仍忙时，不允许新请求或复用缓冲。
- 工程实际 FatFs、USER Driver、Driver Link：在 1:/ 创建/写入/同步文件，清 RAM 重扫后重挂载读回比较。
- 默认 BSP 后端缺失时安全失败；W25Qxx 测试覆盖真实 Bridge 的分区范围、地址转换、异步推进及 NOR 仍忙的显式恢复。

CMake 从实际 ffconf.h 复制全部配置，只删除 MCU/BSP include 以用于主机；不会修改固件配置。MinGW PE 与 ARM ELF 对弱符号的处理不同，默认实现行为测试使用去掉 weak 属性的构建目录副本；固件强弱覆盖由实际 ARM 链接确认。FatFs 集成测试不运行 Service/FreeRTOS/真实 HAL。

测试结果不证明任意欠压、电气损坏或 CRC 碰撞可恢复，也不提供 FatFs 文件事务原子性。上板测试清单和本轮结果见 [FTL 设计](../../docs/flash_ftl_design.md)。
