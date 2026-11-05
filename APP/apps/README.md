# APP/apps

应用接入约定的本体。一个应用是 `APP/apps/<app>/` 一个目录加 `app_table.c` 里的一行；共用层（任务循环、`ui_cell`、文件接口、卷任务与卷事件、注册表）不认识具体应用。规则见 [architecture.apps](../../docs/shape/architecture.apps.md)（ARC-23 至 ARC-28）与 [ADR-0023](../../docs/adr/0023-task-split-and-app-contract.md)。

目前只有音乐，见 [music](music/README.md)。

## 目录约定

- `app_table.h` / `app_table.c`：注册表，应用目录里唯一认识所有应用的地方。
- `<app>/gui/`：应用的 GUI 分区（`gui_init`、`gui_step` 的实现、窗口协议、播放命令与快照归约）。LVGL 的那一半在 `Service/gui/main/<app>/` 与 `view/`，属于 Service 层，不放这里。
- `<app>/catalog/`（音乐独有）：应用自己的数据模块，音乐的曲库在这里（一份表、模块内一把短锁，见 [music/catalog](music/catalog/README.md)）。锁在应用内部，不经 `ui_cell`；共用层不认识它，Storage Task 这时作为它的调用者包含它的头（任务包含应用不受 `check-layer-includes` 限制，应用之间不互相包含）。
- `<app>/foreground/`、后台作业、界面数据与最新需求：随后续票加入同一应用目录，现在还没有。
- 共用层的 `ui_cell`（界面数据短锁）在 `APP/ui_cell/`，不属于任何应用，见 [ui_cell](../ui_cell/README.md)；应用自己的格子结构体放在应用目录里。
- 应用的 README 写它自己的私有 Interface、运行时路径与编译期依赖；共用约定只写在本页。

## 注册表

`app_table[]` 一个应用一行，空字段（`NULL`）由调用方跳过。字段随用到它的票再加；现在有两列：

| 字段 | 谁调用 | 时机 |
|---|---|---|
| `gui_init` | `APP/tasks/gui/gui_task.c` | `Service_GUI_Init()` 成功之后调用一次 |
| `gui_step` | `APP/tasks/gui/gui_task.c` | 每圈 `Service_GUI_ConsumeInput()` 之后、`Service_GUI_Process()` 之前 |

计划中的 `boot_init`、`foreground_step`、`background_step` 尚未存在，不要提前加。GUI 任务与 GUI 模拟器（`Tools/gui_simulator/sim_main.c`）编译并遍历同一份 `app_table.c`，所以模拟器走的就是产品的接入路径。

加一个应用：在 `app_table.c` 包含它的入口头并多写一行；任务循环不改。

## 依赖方向

- 应用可以包含 `APP/tasks/*` 的公开合同与 `Service/*` 的公开头。
- 应用之间不互相包含，唯一例外是设置值的只读头（尚无对应文件）。`scripts/check-layer-includes.ps1` 的 `app` 种类检查这一条，`app_table.*` 直接放在本目录下，不受限。
- 播放和音频读取是音乐专用任务，留在 `APP/tasks/`。新应用默认不加任务。
- `APP/tasks/gui/` 只留循环，不包含任何应用头（只包含 `app_table.h`）。

## 前台规则

前台只表示屏幕正显示哪一页，只有 GUI 侧知道。所有 step 一直被轮流调用，没有 enter/leave 回调；离开前台时应用的 GUI step 改自己的需求状态。0.10.0 只有音乐，分页器不公开当前页。

## 尚未落地的约定

下列约定已由 [#202](https://github.com/immorcoding/mp3_project/issues/202) 的 G 节确定，随各自的票实现，现在代码里还没有：前台一步的时间与文件调用上限、同一应用前台与后台之间的短锁、`.apps_sdram` 内存段、前台持文件数量上限、`boot_init`/`foreground_step`/`background_step` 三列。实现时把现状写进本页。
