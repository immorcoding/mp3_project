# APP

`APP` 是固件的顶层编排 Module：调用 `Platform_Init()`、决定其失败是否致命、创建 FreeRTOS 任务并启动调度器。它不保存 HAL Handle、不构造 Adapter Ops，也不直接操作寄存器。LCD、LED 与触摸控制器的启动初始化属于 `Platform_Init()` 的内部顺序；其中 LED 失败只降级诊断能力，触摸初始化失败当前不阻止启动，GUI 会持续向 LVGL 报告释放且不再访问触摸 I2C。底层 HAL 延时不会阻塞普通任务。

## Core 启动接缝

- `app_init()`：由 `Core/Src/main.c` 的 USER CODE 接缝调用，初始化产品运行所需的 Module。
- `app_error()`：由 `Core/Src/main.c` 的 `Error_Handler()` USER CODE 接缝调用，保留应用级失效安全处理入口。

## APP 内部 Interface

- `app_task_start()`：由 `app_init()` 调用，创建 bootstrap task 并启动 FreeRTOS 调度器；正常情况下不返回。

## 分区与入口（0.10.0，ADR-0023）

APP 下面有四个私有分区，各有自己的 README；改哪个先读哪个：

| 分区 | 内容 | 入口 |
|---|---|---|
| `APP/tasks/` | FreeRTOS 任务入口与任务规格（播放、音频读取、卷、**前台**、后台、Storage、GUI、Log、Monitor） | [tasks](tasks/README.md) |
| `APP/apps/` | 应用接入约定：注册表一行 `{name, boot_init, gui_init, gui_step, foreground_step}`；应用的 GUI 分区、前台流程、曲库在各自目录 | [apps](apps/README.md)、[music](apps/music/README.md) |
| `APP/ui_cell/` | GUI 与任务之间交接界面数据的短锁模块（GUI 不等锁，持锁只拷内存） | [ui_cell](ui_cell/README.md) |

前台一次切（[#218](https://github.com/immorcoding/mp3_project/issues/218)）之后的运行时分工：

- **GUI 任务**（优先级 2）只写最新需求、读结果，不等文件、不查询卷状态；需求与结果经 `ui_cell` 的格子交接。
- **前台任务**（`APP/tasks/foreground/`，优先级 2，栈 1536 word 在 AXI 新段）每圈把注册表里各应用的 `foreground_step` 各调一次，音乐的前台流程（`APP/apps/music/foreground/`）在这里读曲名与 Queue 窗口各一行标签并发布；一步最多一次文件调用加约 1 ms CPU，不碰 Flash 卷（ARC-26）。详见 [前台任务](tasks/foreground/README.md)。
- **优先级**：播放 4 > 音频读取、卷 3 > 前台、GUI、Log、Monitor、Flash 启动 2 > Storage、后台 1 > Boot 0。#218 把 GUI、Log、Monitor 由 1 升到 2、Storage 由 2 降到 1；交错分析见[前台任务](tasks/foreground/README.md)“优先级与交错”，由第 5 次板级验收取证。
- **Boot 任务的内存准备**：`catalog_boot_init()`、`audio_reader_boot_init()`、`ui_cell_boot_init()`、注册表各应用的 `boot_init`（`app_table_boot_init()`）都在创建用到它们的任务之前完成；Boot 栈只有 128 word，往里加初始化调用前先核算最深链（[前台任务](tasks/foreground/README.md)“Boot 栈核算”）。
- **内存段**：`.app_axi_ram`（任务栈与 GUI/前台共享的格子与副本，NOLOAD）、`.apps_sdram`（应用的大缓冲与前台流程状态，NOLOAD，卡在 8 MiB），链接脚本不改；分配与总量见前台任务 README“栈、内存与段”。

## 编译期依赖

- `Platform_*` 产品硬件能力；
- `Service` 目录的公开 Interface；
- FreeRTOS 的任务创建和启动 Interface；
- `Adapters/cortex/cycle_counter` 的 DWT 周期计数 Interface，由 `app_init()` 在 Resource 加载前启动一次；启动早期的硬件 benchmark、GUI 最大帧耗时等只读取得高精度时间；
- CubeMX `Error_Handler()` 等明确的启动接缝。

## 运行时请求路径

APP 是启动与 Task 编排根。它通常发起 `APP → Service → Platform` 的产品请求，但不是每一笔请求的强制中转层；Task 内的后续流程由各自 Module 的公开 Interface 决定。

`Core/Src/main.c` 通过 USER CODE boot seam 调用 `app_init()` 和 `app_error()`；这是生成代码进入 APP 的运行时入口，不表示其他下层 Module 可以包含 APP。

## 事件/ISR 路径

APP Task 只消费 Service 或 Platform 经公开 Interface 发布的普通上下文结果；不拥有 HAL ISR 回调，也不直接处理硬件中断。

## 禁止依赖

- 不包含 Adapter 私有头、HAL Handle、GPIO/SDMMC/I2S 寄存器细节；
- 不直接调用 `HAL_SD_*`、`BSP_SD_*`、`f_*`；
- 不实现器件协议、总线时序或板级 Adapter Bind。

## 命名

`APP` 是一个完整顶层 Module，`APP/tasks` 只是其私有 Implementation 分区。`app_init()`、`app_error()` 是 Core 进入 APP 的启动接缝，`app_task_start()` 与各 Task 入口仅在 APP 内调用；它们都使用 `snake_case`。下层 Module 不得包含 APP 头文件或调用其函数；需要被多处复用的产品能力应下沉为 Service Interface。详情见 [../CODING_STANDARDS.md](../CODING_STANDARDS.md)。
