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
- FatFs 集成（`flash_ftl_fatfs_remount`）不运行 Service、FreeRTOS 或真实 HAL。
- 默认 BSP 后端缺失时安全失败；测试显式保留 assert，不受 Release 的 `NDEBUG` 影响。
- 完成等待（`filesystem_completion_wait`，`test_filesystem_completion.c`）：真实的 Filesystem Service、SD/Flash 私有执行器、FatFs 与 USER DiskIO 胶水，加共用 FreeRTOS 桩（二值信号量、阻塞钩子、tick），只有 Platform SD/Flash 用测试内替身（Flash 替身带 RAM 盘）。阻塞钩子在“等待将要发生”时替中断调用已订阅的回调，所以能覆盖：
  - 成功：SD 读/写（含多分块）、Flash 读/写/同步/诊断操作在中断给出信号后完成；回调只用 `FromISR` 版本，并把“唤醒了更高优先级任务”交给 `portYIELD_FROM_ISR`。
  - 超时：SD 等满预算后向 Platform 提交 ERROR；Flash 预算到期先 Abort，再继续 Process 直到静止，最后报超时。
  - 错误：错误/中止/方向不符的事件，Process 报错，Start 被拒绝。
  - 迟到的旧信号：超时之后才到的完成中断，不会让下一次传输或下一次 Flash 等待白白过去。
  - 创建时机：信号量先于向 Platform 订阅创建；初始化之前传输函数失败；重复初始化不重建信号量、不丢信号。
  - 不依赖调用任务：别的任务格式化、挂载并读写 Flash 卷，磁盘状态与初始化它的任务读到同一个答案（旧实现里这里是 `STA_NOINIT`）。
  - 保真边界：主机单线程，桩不切换任务也不真阻塞；通过不证明真实 ISR 时序、优先级、DMA 与 Cache 行为，也不证明迟到 DMA 完成事件在板上不会覆盖 bounce buffer（那是显式 Abort 的范围）。
- 共用 FreeRTOS 桩 [`Tests/stubs/freertos/`](../stubs/freertos/README.md) 的自测（`freertos_stub_behavior`）：互斥锁、二值信号量和任务通知的行为，中断里 Give 的单独计数与“唤醒了更高优先级任务”的脚本化、`portYIELD_FROM_ISR` 的让出计数、`vTaskDelay`，以及“拿不到锁”“关闸时等待”的脚本化；文件基准用它的假 tick。

测试结果不证明任意欠压、电气损坏或 CRC 碰撞可恢复，也不提供 FatFs 文件事务原子性。

上板测试清单与结果见 [Flash FTL 设计](../../docs/shape/storage.durability.md)。
