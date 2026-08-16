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

当前显示硬件的最小验证由 Platform 与 LCD Task 共同完成：`Platform_Init()` 在调度器启动前初始化 ST7789、安装 SPI HAL 回调，再复位并探测 FT6X36；LCD Task 读取触摸 Chip ID，并使用其拥有的 SDRAM 单帧缓冲区通过 `Platform_LCD_*` 验证 SPI DMA 分块刷新和最终 EOT 通知。触摸坐标、LVGL 输入回调、TP IRQ 订阅、正式双绘制缓冲和低功耗唤醒尚未接入。

完整分层与依赖规则见 [docs/architecture_standard.md](docs/architecture_standard.md)，命名与注释规则见 [docs/coding_standard.md](docs/coding_standard.md)，稳定领域术语见 [CONTEXT.md](CONTEXT.md)。`Core`、`Drivers`、`Middlewares`、`USB_DEVICE` 和 `FATFS/` 主要由 CubeMX 或第三方维护；不要把产品策略直接写入其中。FatFs 的 `BSP_SD_*` 强定义是外部 Override Seam，其实现放在 `Service/filesystem`，不修改生成的 DiskIO Glue。

阅读分层时必须区分三件事：`Vendor → Adapters → Components → Platform → Service → APP` 表示功能/抽象所有权；编译期 `#include` 按 Interface 所有权和装配需要决定；运行时请求通常向下、硬件事件经已注册回调向上。第三方回调或 Override Seam（例如 FatFs `disk_* → BSP_SD_*`）是运行时入站接缝，不表示反向头文件依赖。

`README.md` 用于导航和局部工作入口；`CONTEXT.md` 用于领域术语和产品职责；`docs/` 用于跨 Module 的技术事实。三者互相链接，不重复维护同一份调用链或实现细节。
