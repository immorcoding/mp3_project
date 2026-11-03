# FTL 主机回归

测试仅操作进程内 Fake NOR，不访问板卡；通过公开 FTL/RawOps 接口验证行为，真实 FatFs 集成在 BSP 契约处替换硬件与 RTOS 环境。运行方法见 [主机回归操作手册](../README.md)。

## 范围

- FTL 基本读写、部分/跨组更新、首尾和溢出边界、忙时拒绝、未映射 0xFF、持久化后重建 RAM。
- 持续覆盖写入会触发前台 GC；每次最多回收一块，达到高水位后停止；旧块 GC 在写擦阶段撕裂不会误报卷损坏。
- 页编程、格式化和 GC 写擦阶段撕裂；无有效提交的数据不可见，最新已提交版本损坏会报错，虚假擦除成功由全块校验发现。
- 全 FF 数据页跳过编程；异常后 Quiesce 仍忙时，不允许新请求或复用缓冲。
- 工程实际 FatFs、USER Driver 与 Driver Link：在 `1:/` 创建、写入、同步并在清 RAM 重扫后重挂载读回比较。
- CMake 从工程实际 `ffconf.h` 复制配置，仅去除不参与 FatFs 算法的 MCU/BSP `#include`。
- MinGW PE 与 ARM ELF 对 weak 符号的处理不同：默认实现行为测试使用去掉 weak 属性的构建目录副本；ARM 强弱覆盖由实际链接确认。
- Filesystem 私有锁（[#191](https://github.com/immorcoding/mp3_project/issues/191)，可丢弃分支）：`test_filesystem_lock.c` 同一份源码编译两次（全局一把、`SPIKE_LOCK_PER_VOLUME=1` 的每卷一把加表锁），验证锁记账——最长持锁与最长等锁带任务名和操作名、窗口重置只清窗口、每个任务最近一次持锁时长、GUI 或播放任务误调文件接口只计数并记一次 WARN、锁个数与名字。`stubs/` 下的互斥量替身在同一把锁被重复获取时中止进程，所以经公开入口的 Service 用例也同时证明公开函数之间没有重复拿锁。
- FatFs 集成不运行 Service、FreeRTOS 调度器或真实 HAL；主机是单线程的，真实并发、优先级继承和等锁时长只能上板。
- 默认 BSP 后端缺失时安全失败；测试显式保留 assert，不受 Release 的 `NDEBUG` 影响。

测试结果不证明任意欠压、电气损坏或 CRC 碰撞可恢复，也不提供 FatFs 文件事务原子性。

上板测试清单与结果见 [Flash FTL 设计](../../docs/shape/storage.durability.md)。
