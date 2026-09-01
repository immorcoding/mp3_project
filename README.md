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
- W25Q256 已完成 CubeMX QSPI 配置，以及 Component、STM32 HAL Adapter、Platform 的间接模式 JEDEC ID 启动识别、`EF / 19` 厂商容量校验、SFDP 签名校验、SR1/SR2 同步读取、启动期 QE 按需安全置位、`0xEC` 固定 4-byte Quad I/O 读取和 `0x34` 非阻塞 Quad 页编程状态机；同时已有 `0x21` 4 KiB 擦除、QSPI/MDMA 读取、WIP 自动状态轮询和只读内存映射路径，详见 [docs/w25q256_architecture.md](docs/w25q256_architecture.md)。Flash FTL、Bridge、Platform 装配、Service 同步执行器与 USER DiskIO 已实现，SD/Flash 私有目录已分离；主机回归及 Debug/Release 构建通过，真实掉电与板级验收待完成，见 [docs/flash_ftl_design.md](docs/flash_ftl_design.md)。

## 构建

构建环境需要 CMake、Ninja 与可从命令行找到的 Arm GNU Toolchain（`arm-none-eabi-*`）。在工程根目录执行：

```powershell
./scripts/build-firmware.ps1 -Configuration Debug
```

```powershell
./scripts/build-firmware.ps1 -Configuration Release
```

分别生成到 `build/firmware-debug/` 和 `build/firmware-release/`。CMake Preset 仍可供 IDE 使用；日常命令使用上述脚本。

## 分层检查

`Components/` 与 `Adapters/bridge/` 不得包含 HAL、FreeRTOS、CubeMX 头或上层目录。`Service/` 不得包含 HAL、`main.h`、`Drivers/` 或 HAL Adapter；FreeRTOS、FatFs Glue、LVGL 与 `GUI/` 导出头仍允许。单独检查：

```powershell
./scripts/check-layer-includes.ps1
```

`./scripts/verify.ps1` 会先跑该项，再构建固件并跑主机回归。

克隆后执行一次 `./scripts/install-git-hooks.ps1`，之后每次 `git commit` 会自动跑分层检查，失败则拒绝提交。完整固件构建与主机回归仍需手动执行 `verify.ps1`，上板测试不在 hook 范围内。

## 主机回归

主机回归的环境要求、运行命令和安全边界见 [Tests/README.md](Tests/README.md)。

## 交流与审阅重点

本仓库目前主要用于嵌入式课程交流与架构审阅。特别欢迎针对以下方面提出建议：

- Module 所有权、Interface 和 Adapter 接缝是否足够清晰；
- FreeRTOS 任务、ISR 回调、DMA 与 Cache 一致性的运行时路径是否合理；
- 后续音频解码、外部 Flash、USB MSC 与 GUI 功能继续接入时的演进方式；
- 注释、文档、命名和可复现构建体验中的不足。
