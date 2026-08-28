# ADR-0008：SDRAM 早期初始化与 NOLOAD 缓冲区所有权

- 状态：已接受（按现有代码追溯记录）
- 日期：2026-08-29
- 相关实现说明：[../sdram_architecture.md](../sdram_architecture.md)

## 背景

链接脚本将大绘制缓冲放在外部 SDRAM 的 `.sdram_framebuffer (NOLOAD)` 段。C 运行库处理 `.data/.bss` 前，该段可能已经被访问，因此外部 SDRAM 必须在早期可用；但正式运行仍需要按最终 FMC 时钟重新初始化。

## 决定

1. `Core/Src/fmc.c` 的 USER CODE 早期初始化接缝只使用局部状态和 HAL，在 `SystemInit()` 后、C 运行库启动循环前建立安全的临时 SDRAM 状态；它不进入 APP、Service、Platform 或 FreeRTOS。
2. 进入 `main()` 后，由 `Platform/sdram` 借用 CubeMX `hsdram1` 按最终 130 MHz 参数完成正式 JEDEC 初始化，并向上提供 SDRAM 诊断能力。
3. `.sdram_framebuffer (NOLOAD)` 不从 Flash 加载也不自动清零；每个缓冲区拥有者必须在首次 CPU 或 DMA 使用前完整初始化。
4. 全容量 SDRAM 诊断是破坏性操作，只允许在外部堆、LVGL 帧缓冲、音频缓存或 DMA 缓冲使用前独占执行；不得借此把 FreeRTOS Heap、任务栈或普通 `.bss` 自动迁入 SDRAM。

## 后果

- 启动期的外部存储访问安全与运行期 Platform 语义分离；
- 大缓冲不会增加 Flash 初始化搬运量，但所有权与首次写入责任必须明确；
- SDRAM 频率、刷新计数、MPU/Cache 属性与长期缓冲位置变更均需同步核对技术文档与实测。
