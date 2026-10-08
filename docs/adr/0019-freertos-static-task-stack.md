# ADR-0019：播放任务采用静态分配，解码栈放 DTCM

- 状态：已接受。
- 日期：2026-10-08
- 相关 ticket：[0.6.0 地图](https://github.com/immorcoding/mp3_project/issues/29)、[解码内存预算总账](https://github.com/immorcoding/mp3_project/issues/39)、[解码 scratch 出堆](https://github.com/immorcoding/mp3_project/issues/40)
- 相关决定：[ADR-0017](0017-mp3-decoder-minimp3.md)（minimp3 与解码器状态）、[ADR-0006](0006-cache-range-ownership.md)（Cache 范围）
- 完整布局：[地图上的内存布局记录](https://github.com/immorcoding/mp3_project/issues/29#issuecomment-6059217048)

## 背景

minimp3 的解码 scratch（`mp3dec_scratch_t`，ARM 上 16236 字节）是 `mp3dec_decode_frame` 的局部变量，必须落在调用它的任务栈上，与 ADR-0017 第 3 条一致。FreeRTOS 的动态任务栈与任务控制块都从堆分配，而堆只有 32 KiB。现有的应用任务、空闲任务与定时器守护任务已占用约 17.5 KB 堆（含块头）；若播放任务的栈也从堆分配，堆需求至少为 33928 字节，超出至少 1160 字节。

## 决定

1. 播放任务用 `xTaskCreateStatic` 创建，任务控制块与栈均为静态数组。内核启用静态分配，动态分配保持启用，现有动态任务不改。
2. 静态栈与任务控制块放在 DTCM。DTCM 不经 D-Cache，DMA 也访问不到，因此不需要 Cache 维护，也不触发 ADR-0006 的缓冲范围约束。
3. 解码 scratch 保持为局部变量。每个解码任务各有一份，每个解码器持有自己的 `mp3dec_t`，因此可以并发解码。minimp3 不修改，仍按 ADR-0017 的版本固定策略升级。
4. 堆大小不变，不扩堆。

## 未采用的方案

- **扩大堆到约 40 KiB。** 堆压力仍然存在，scratch 仍是堆上的栈，只是多占了 DTCM，没有解决问题。
- **对 minimp3 打本地补丁，让 scratch 由调用方静态持有。** 这是第三方代码改动，要承担补丁与升级成本。静态 scratch 只能串行使用，失去了任务级并发能力。
- **把栈放在 AXI SRAM。** 空间够用，但 AXI 栈要经过 ADR-0006 的 Cache 范围审查，还要防止它与相邻 DMA 缓冲共享缓存行，没有带来收益。

## 后果

- 播放任务的任务控制块与栈在启动时确定，运行期间不变。DTCM 为播放任务新增约 16 KB 静态内存（栈下限 16240 字节，加任务控制块 116 字节）。
- 堆余量不少于 15 KB，足以容纳后续新增的队列。
- 栈大小在实施时要补足 Layer I/II 分支的局部变量与调用链余量，并用栈水位复核。
- 新增栈仍须按 [HWD-2](../shape/hardware.md) 审查启动与 MPU。Cache/DMA 一项对 DTCM 不适用。
- `FreeRTOSConfig.h` 的静态分配开关因此改变，这是本决定的一部分，已由用户在决定票中选定。
