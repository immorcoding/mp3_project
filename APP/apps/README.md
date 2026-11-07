# APP/apps

应用接入约定的本体。一个应用是 `APP/apps/<app>/` 一个目录加 `app_table.c` 里的一行；共用层（任务循环、`ui_cell`、文件接口、卷任务与卷事件、注册表）不认识具体应用。规则见 [architecture.apps](../../docs/shape/architecture.apps.md)（ARC-23 至 ARC-28）与 [ADR-0023](../../docs/adr/0023-task-split-and-app-contract.md)。

目前只有音乐，见 [music](music/README.md)。

## 目录约定

- `app_table.h` / `app_table.c`：注册表，应用目录里唯一认识所有应用的地方。
- `<app>/gui/`：应用的 GUI 分区（`gui_init`、`gui_step` 的实现、最新需求的写入与结果的读取、播放命令与快照归约）。LVGL 的那一半在 `Service/gui/main/<app>/` 与 `view/`，属于 Service 层，不放这里。
- `<app>/catalog/`（音乐独有）：应用自己的数据模块，音乐的曲库在这里（一份表、模块内一把短锁，见 [music/catalog](music/catalog/README.md)）。锁在应用内部，不经 `ui_cell`；共用层不认识它，Storage Task（扫描）和前台任务（`catalog.c` 发布后叫醒前台）作为它的调用者包含它的头（任务包含应用不受 `check-layer-includes` 限制，应用之间不互相包含）。
- `<app>/foreground/`：应用的前台流程（`foreground_step` 的实现，行缓存与窗口组装；音乐的在 [music](music/README.md)“前台流程”）。`<app>_cells.h/.c`：应用的界面数据格子（需求与结果的结构体与存储），`<app>_app.c`：`boot_init`。后台作业随后台任务票加入，现在还没有。
- 共用层的 `ui_cell`（界面数据短锁）在 `APP/ui_cell/`，不属于任何应用，见 [ui_cell](../ui_cell/README.md)；应用自己的格子结构体放在应用目录里。
- 应用的 README 写它自己的私有 Interface、运行时路径与编译期依赖；共用约定只写在本页。

## 注册表

`app_table[]` 一个应用一行，空字段（`NULL`）由调用方跳过。字段随用到它的票再加；现在有五列（#216 加 `name`、`gui_init`、`gui_step`，#218 加 `boot_init`、`foreground_step`）：

| 字段 | 谁调用 | 时机 |
|---|---|---|
| `name` | 前台任务的 step 耗时日志 | 日志里标明是哪个应用的 step 慢（短名，例如 `music`） |
| `boot_init` | `APP/tasks/app_tasks.c`（Boot 任务，`app_table_boot_init()`） | 创建前台任务和 GUI 任务**之前**调用一次：初始化应用的界面数据格子、缓存键表、`.apps_sdram` 里的组装缓冲。栈只有 128 word，函数要浅，不调文件接口 |
| `gui_init` | `APP/tasks/gui/gui_task.c` | `Service_GUI_Init()` 成功之后调用一次 |
| `gui_step` | `APP/tasks/gui/gui_task.c` | 每圈 `Service_GUI_ConsumeInput()` 之后、`Service_GUI_Process()` 之前；不等文件、不查询卷状态 |
| `foreground_step` | `APP/tasks/foreground/foreground_task.c` | 前台任务每圈每个应用恰好一次；返回“多少 ms 后再叫我”（0 马上，`FOREGROUND_WAIT_FOREVER` 等通知），取所有应用里的最小值 |

计划中的 `background_step` 尚未存在，不要提前加。GUI 任务与 GUI 模拟器（`Tools/gui_simulator/sim_main.c`）编译并遍历同一份 `app_table.c`（调用 `app_table_boot_init()`，并在每圈里替身前台任务调用 `foreground_step`），所以模拟器走的就是产品的接入路径；模拟器的前台后端是脚本化的假实现（`Tools/gui_simulator/fakes/music_sim.c`，见模拟器 README）。

加一个应用：在 `app_table.c` 包含它的入口头并多写一行（`name`、`boot_init`、`gui_init`、`gui_step`、`foreground_step`）；任务循环、栈和 Monitor 登记都不改（所有应用共用前台任务和它的栈）。

## 前台一步的规则（ARC-26）

`foreground_step` 在所有应用共用的前台任务里运行，一步太长会拖住别的应用：

- **一步最多一次文件调用加约 1 ms CPU**（初值，板测修正），step 里不放大局部变量，**不碰 Flash 卷**（后台写 32 KiB 约 1.5 s，前台等就全停）。每个应用同一时刻持有的文件数最多 1 个（读完即关，不跨步持有），这是 SD 4 个文件槽核算的前提（音频流 1 + 前台 1，曲库扫描开目录不占文件槽）。
- 一步要多次文件调用的活（读 12 行标签）拆成多步，每步做一点，返回 0（马上再调）或 1 ms；活做完返回 `FOREGROUND_WAIT_FOREVER` 等需求或事件叫醒。没活时必须睡，不能返回 0 空转。
- 重活（CPU 吃重、Flash）进后台任务，不进前台。
- 前台任务量每个 step 的耗时，超过 50 ms 记一行 WARN（`Foreground step slow app=<name> us=<N> limit=50000`），只记录不停机，没有锁或断言能拦住长 step，所以靠这行发现（见 [前台任务](../tasks/foreground/README.md)）。
- 前台**自己**一步读不到需求（`ui_cell` 的读被占着没读到）时返回 1 ms 自己重试，因为没有人会再叫醒它（`ui_cell_changed()`）。
- 需求格是状态不是动作（连按翻页由 GUI 算出目标页再写），新的覆盖旧的；曲名和封面这类结果带回所答的需求序号，GUI 只采信最新的，窗口自带起点、代次和条数，不比序号（#200）。

## 内存段

应用的大块缓冲与跨任务交接数据按用途放段（HWD-2，链接脚本一个字没改）：

- `.app_axi_ram`（AXI SRAM，NOLOAD）：静态创建的任务栈和 GUI 与前台任务共享的数据（界面数据格子、GUI 一侧的结果副本）。NOLOAD 不清零：格子由 `boot_init` 整块写入，栈由 `xTaskCreateStatic` 填充。
- `.apps_sdram`（SDRAM，NOLOAD，由链接脚本的 `ASSERT` 卡在 8 MiB 以内）：应用的缓冲和状态，现在只有音乐前台流程的：行缓存的行内容与键表、窗口组装缓冲、曲名组装区与临时一行、曲库读全结果、流程状态变量（`MUSIC_SDRAM_SECTION`，40224 B，不放 DMA 缓冲，只经 CPU 读写）。ARC-28：应用内存默认放这里，DTCM 留给任务栈与内核；step 里不放大局部变量（临时的大对象也放这里）。启动自检之后才有主，`boot_init` 在 SDRAM 自检之后整块写入。
- DTCM `.bss`：小的状态（缓存键表要求启动时清零，所以放这里）。

分配与总量见 [前台任务](../tasks/foreground/README.md)“栈、内存与段”。

## 依赖方向

- 应用可以包含 `APP/tasks/*` 的公开合同与 `Service/*` 的公开头。
- 应用之间不互相包含，唯一例外是设置值的只读头（尚无对应文件）。`scripts/check-layer-includes.ps1` 的 `app` 种类检查这一条，`app_table.*` 直接放在本目录下，不受限。
- 播放和音频读取是音乐专用任务，留在 `APP/tasks/`。新应用默认不加任务。
- `APP/tasks/gui/` 只留循环，不包含任何应用头（只包含 `app_table.h`）。

## 前台规则

前台只表示屏幕正显示哪一页，只有 GUI 侧知道。所有 step 一直被轮流调用，没有 enter/leave 回调；离开前台时应用的 GUI step 改自己的需求状态。0.10.0 只有音乐，分页器不公开当前页。

## 尚未落地的约定

下列约定已由 [#202](https://github.com/immorcoding/mp3_project/issues/202) 的 G 节确定，随各自的票实现，现在代码里还没有：`background_step` 列、同一应用前台与后台之间的短锁、后台任务的一步规则。实现时把现状写进本页。
