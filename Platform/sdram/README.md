# Platform SDRAM

该 Module 表示当前 PCB 上兼容 `MT48LC16M16A2-6A` 与 `AS4C16M16SA-7TCN` 的 32 MiB x16
SDRAM。它持有 CubeMX 的 `hsdram1`，在 FMC 控制器配置完成后执行二者共用的 JEDEC 上电序列，
并提供启动阶段的破坏性硬件诊断。

## 公开 Interface

- `Platform_SDRAM_Init()`：执行时钟使能、预充电、自动刷新、模式寄存器和刷新率配置；
- `Platform_SDRAM_RunDiagnostic()`：验证数据线、地址线、全 32 MiB 地址图样，并统计 CPU
  写入提交和冷读校验的吞吐。

## 编译期依赖

- CubeMX `Core/Inc/fmc.h` 的 `hsdram1` 和 HAL SDRAM Command Interface；
- `Adapters/cortex/cache` 的 D-Cache 维护 Interface；
- `Adapters/cortex/cycle_counter` 的 Cortex-M7 周期计数 Interface。

## 运行时请求与事件路径

`Platform_Init()` 在任何任务创建前调用初始化。Storage Task 的可选启动诊断经公开
Interface 请求 Platform SDRAM；SDRAM 没有中断或任务通知路径。

## 资源与约束

- 基地址为 `0xC0000000`，容量固定为 32 MiB；
- `Platform_SDRAM_RunDiagnostic()` 会覆盖整个容量，不能与 LVGL 帧缓冲、堆或任何业务数据并发；
- 本 Module 不负责链接器段放置。只有验证通过并单独审查启动时序后，才可把 `.bss`、堆或帧缓冲放入 SDRAM；
- `Core/Src/fmc.c` 的 `SDRAM_EarlyInit()` 是启动接缝，不属于本 Module 的公开
  Interface。它在 `.data/.bss` 启动循环前使用临时约 32 MHz 时序使 SDRAM 可访问，
  并按 AS4 的较长要求等待至少 200 µs；
  随后 CubeMX 和本 Module 仍会按最终 135 MHz 参数完成正式初始化；两阶段之间的
  SDRAM 内容不应被依赖；
- 链接脚本当前预留 `.sdram_framebuffer (NOLOAD)`。该段不加载、也不自动清零；其
  未来拥有者必须在正式 SDRAM 初始化后、首次交给 LCD/DMA 之前完整写入缓冲区。

## 禁止依赖

- 不包含 APP、Service 或 FreeRTOS；
- 不向上泄漏 `SDRAM_HandleTypeDef`、FMC 命令类型或寄存器；
- 不承担 DMA、帧缓冲或动态内存分配策略。

## 命名

对上使用 `Platform_SDRAM_*`；文件内辅助函数使用 `platform_sdram_*`。
