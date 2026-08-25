# project_mp3

基于 STM32H743 的便携式媒体播放器固件。

## 自维护代码结构

```text
APP/          启动编排和 FreeRTOS 任务入口
Service/      文件系统、异步日志等产品流程 Module
Platform/     当前 PCB 的设备装配与产品硬件能力
Components/   与 MCU 无关的协议、设备和算法核心
Adapters/     STM32 HAL、Cortex-M 与跨 Component Bridge 的具体实现
FATFS/        CubeMX FatFs 逻辑卷与 DiskIO Glue；项目 Override Seam 由 Service/filesystem 实现
```

`Platform_Init()` 在调度器启动前初始化 ST7789、安装 SPI HAL 回调，再复位并探测 FT6X36；GUI Task 经 `Service_GUI_*` 驱动 LVGL 的双绘制缓冲、SPI DMA 刷新与轮询式单指触摸输入。触摸的坐标方向由 GUI Service 解释；手势、多指、TP IRQ 订阅和低功耗唤醒尚未接入。

完整分层与依赖规则见 [docs/architecture_standard.md](docs/architecture_standard.md)，命名与注释规则见 [docs/coding_standard.md](docs/coding_standard.md)，稳定领域术语见 [CONTEXT.md](CONTEXT.md)。`Core`、`Drivers`、`Middlewares`、`USB_DEVICE` 和 `FATFS/` 主要由 CubeMX 或第三方维护；不要把产品策略直接写入其中。FatFs 的 `BSP_SD_*` 强定义是外部 Override Seam，其实现放在 `Service/filesystem`，不修改生成的 DiskIO Glue。

阅读分层时必须区分三件事：`Vendor → Adapters → Components → Platform → Service → APP` 表示功能/抽象所有权；编译期 `#include` 按 Interface 所有权和装配需要决定；运行时请求通常向下、硬件事件经已注册回调向上。第三方回调或 Override Seam（例如 FatFs `disk_* → BSP_SD_*`）是运行时入站接缝，不表示反向头文件依赖。

`README.md` 用于导航和局部工作入口；`CONTEXT.md` 用于领域术语和产品职责；`docs/` 用于跨 Module 的技术事实。三者互相链接，不重复维护同一份调用链或实现细节。

## 当前状态

- 已接入 LVGL v8.3.11、SquareLine Studio 1.6.1 导出的 GUI 原型、ST7789 SPI DMA 刷新和 FT6X36 单指触摸；
- GUI 的运行时入口为 `APP/tasks/gui/gui_task.c`，其显示与输入装配由 `Service/gui` 持有；
- `GUI/` 是 SquareLine 的生成目录。应在 SquareLine 编辑器中修改页面、资源和交互，再导出；不要直接手改其中的 C 源或资源清单；
- 外部 SDRAM 当前以 130 MHz 配置，LVGL 双绘制缓冲位于 `.sdram_framebuffer (NOLOAD)` 段。

## 构建

构建环境需要 CMake、Ninja 与可从命令行找到的 Arm GNU Toolchain（`arm-none-eabi-*`）。在工程根目录执行：

```text
cmake --preset Debug
cmake --build --preset Debug
```

Release 构建可将两个 `Debug` 替换为 `Release`。生成的 ELF、HEX、BIN 位于对应的 `build/<preset>/` 目录。

## 交流与审阅重点

本仓库目前主要用于嵌入式课程交流与架构审阅。特别欢迎针对以下方面提出建议：

- Module 所有权、Interface 和 Adapter 接缝是否足够清晰；
- FreeRTOS 任务、ISR 回调、DMA 与 Cache 一致性的运行时路径是否合理；
- 后续音频解码、外部 Flash、USB MSC 与 GUI 功能继续接入时的演进方式；
- 注释、文档、命名和可复现构建体验中的不足。
