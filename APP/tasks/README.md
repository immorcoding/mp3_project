# APP Tasks

本目录包含由 `app_task_start()` 创建的 FreeRTOS Task 入口和任务规格。任务创建顺序与名称在 `app_tasks.c`，栈深度和优先级集中在 `APP/app_config.h`，各叶目录只维护本任务的运行逻辑。

## APP 内部 Interface

- `app_task_start()`：由 `APP/app.c` 创建 bootstrap task 并启动调度器；
- `log_task()`、`storage_task()`、`audio_reader_task()`、`volume_task()`、`background_task()`、`foreground_task()`、`playback_task()`、`monitor_task()`、`gui_task()`：仅作为 FreeRTOS `TaskFunction_t` 交给创建器。播放任务用 `xTaskCreateStatic` 创建（TCB 与栈在 `app_tasks.c` 的 DTCM `.bss`）；卷任务同样静态创建，TCB 在 `.bss`，栈放 AXI SRAM 新段 `.app_axi_ram`；音频读取任务由 `audio_reader_task_create()` 静态创建（`audio_reader/audio_reader_task.c`，栈 4 KiB 同样在 `.app_axi_ram`），块池和两个信箱由 Boot 里的 `audio_reader_boot_init()` 先初始化；后台任务由 `background_task_create()` 静态创建（`background/background_task.c`，栈 6 KiB 同样在 `.app_axi_ram`）；一次性的 Flash 启动任务由卷任务静态创建（`volume/flash/volume_flash_task.c`，栈同样在 `.app_axi_ram`），其余任务仍用 `xTaskCreate`；`.app_axi_ram` 现有 18432 B（卷 4096 + 音频读取 4096 + Flash 启动 4096 + 后台 6144）；
- `foreground_task_wake()`（`foreground/foreground_task.h`）：APP 内部接口（#202 M 节授权），叫醒前台任务把所有应用的前台 step 各调一遍；任何任务上下文可调，不得从 ISR 调，见 [前台任务](foreground/README.md)；
- `background_task_wake()`（`background/background_task.h`）：APP 内部接口（#202 M 节授权），叫醒后台任务把所有作业各调一遍；任何任务上下文可调，不得从 ISR 调，见 [后台任务](background/README.md)；
- `playback_command_send()`、`playback_snapshot_read()`：播放任务公开合同（`playback/playback_contract.h`），只给音乐应用 GUI 分区（`APP/apps/music/gui/`）和模拟器脚本化后端使用，见 [播放任务](playback/README.md)。

本目录不是独立的跨 Module Interface；其头文件和任务入口只服务于顶层 `APP` Module，其他层不得包含或调用。

## 编译期依赖

- 同目录任务配置宏（栈与优先级在 `APP/app_config.h`）；
- `Service`、`Platform` 的公开 Interface；
- FreeRTOS Task、通知和诊断 Interface。

## 运行时请求与事件路径

Task 是 APP 创建后的运行时执行上下文：任务通过 Service/Platform 的公开 Interface 发起请求，硬件 ISR 只经已注册回调或 FromISR 原语唤醒对应任务。任务目录不拥有 HAL ISR 入口。

## 任务与优先级

| 任务 | 优先级 | 栈（word） | 创建方式 | 职责 |
|---|---|---|---|---|
| Playback Task | 4 | 5120 | `xTaskCreateStatic`，DTCM | 唯一的播放状态机；解码、PCM 半区补满与提交、快照发布 |
| Audio Read Task | 3 | 1024 | `xTaskCreateStatic`，栈在 AXI `.app_axi_ram` | 读 SD 上的音频文件、填块池，到安全水位后用唯一的等待点让路；开流、探测标签、跳转重开流、拔卡写 `CARD_REMOVED`、`SERVICE_BUSY` 重试都在这里，不解码，见 [音频读取任务](audio_reader/README.md) |
| Volume Task | 3 | 1024 | `xTaskCreateStatic`，栈在 AXI `.app_axi_ram` | 卡检测消抖、关闸、对账、SD 挂载与卸载、发布卷事件（不碰具体的文件读写），见 [卷任务](volume/README.md) |
| **Foreground Task** | **2** | **1536** | `xTaskCreateStatic`，栈在 AXI `.app_axi_ram` | 每圈把注册表里各应用的前台 step 各调一次，取最小返回值当睡眠时长，任何通知提前叫醒，量每个 step 的耗时（WARN > 50 ms，只记录不停机）；现在只有音乐的前台流程（读曲名与窗口行标签，经 `ui_cell` 发布），见 [前台任务](foreground/README.md) |
| GUI Task | 2（#218：1 → 2） | 2048 | `xTaskCreate` | LVGL 唯一执行上下文；每圈结束后 `vTaskDelay` 睡到下一个 LVGL 定时器到期；不等文件、不查询卷状态 |
| Log Task | 2（#218：1 → 2） | 512 | `xTaskCreate` | 日志消费 |
| Monitor Task | 2（#218：1 → 2） | 256 | `xTaskCreate` | 结温与板级监控 |
| Flash Boot Task（一次性） | 2 | 1024（约 4 KiB） | `xTaskCreateStatic`，栈在 AXI `.app_axi_ram` | Flash 基准、挂载、无文件系统时格式化、发 Flash 卷就绪，打栈水位后自删；卷任务创建、不等它 |
| Storage Task | 1（#218：2 → 1） | 1024 | `xTaskCreate` | 卷事件的订阅者：曲库扫描、Flash 回收；逐步拆给后台任务（音频流已搬到音频读取任务，Queue 窗口与曲名两个槽已搬到前台任务） |
| Background Task | 1 | 1536 | `xTaskCreateStatic`，栈在 AXI `.app_axi_ram` | 每圈把注册的后台作业各调一次，取最小返回值当睡眠时长，任何通知提前叫醒；现在只有默认关闭的 Flash 压力钩子，默认构建里没有作业、一直阻塞，见 [后台任务](background/README.md) |
| Boot（Create Task） | 0 | 128 | `xTaskCreate` | 创建其余任务后自删 |

排序依据（[#71](https://github.com/immorcoding/mp3_project/issues/71) A6、B1、B2；[#62](https://github.com/immorcoding/mp3_project/issues/62)）：

- **播放最高（4）**：PCM 半区只有约 24 ms（1152 帧，48 kHz）的补满期限，期限只允许受中断与播放任务自身工作（有上界的一次唤醒，不含 FatFs/LVGL、不同步等待 Storage、发送不阻塞）影响。播放不碰文件、不拿任何文件锁（Filesystem 的调用者拒绝名单，入口断言停机），所以卷锁上的优先级反转挡不到它；升到 4 是为了卷任务（3）做对账时播放仍能抢占它。Storage 的一次 FatFs 读取（含 SD DMA 与可能的单分块 500 ms 超时）和 GUI 的一帧 `lv_timer_handler` 都可能远长于期限，所以播放任务必须能抢占它们。播放与这些长任务的关系只靠优先级：播放的期限不依赖 GUI 或文件读取中途让出，播放任务也不与低优先级任务共用互斥锁。GUI 一帧中途不加让出点；每圈结束后至少睡到下一个 tick，这是给同级和低优先级任务留时间，不是为了播放（睡多久见 [GUI Task](gui/README.md)）。
- **音频读取 3**：块池缓冲有限，补块不能被 Storage、前台、GUI 的长活或 Flash 写入拖住；与卷任务、定时器服务任务同级（时间片轮转），低于播放（4）、高于前台（2）。读取任务放 SD 锁后马上又能拿到，优先级 2 的前台（和优先级 1 的 Storage）排不上，所以水位以上每读一块用唯一的等待点让 1 tick（见 [音频读取任务](audio_reader/README.md)）。读取任务持 SD 锁时卷任务要刷新，等那一次调用做完（同级，没有优先级继承）；放锁后读取任务的下一次文件调用在入口等闸。
- **卷任务 3**：用户拔卡要马上停，对账不能被 Storage、GUI 的长活拖住。它与定时器服务任务同级；等 SD 卷锁时持锁的 Storage 或前台任务经优先级继承临时升到 3，优先级 1、2 与 3 之间没有别的任务能抢走它，反转的上界是那一次调用的持锁时间（见 [卷任务](volume/README.md)“优先级反转在卷锁上的表现”）。
- **前台、GUI、Log、Monitor、Flash 启动任务同级 2**（#218，用户 2026-11-06 的票面：前台 2，GUI、Log、Monitor 由 1 升到 2，Storage 由 2 降到 1）：时间片轮转（`configUSE_TIME_SLICING` 为 1，1 ms）。前台任务做“读一行文件、拷内存、发布界面数据”，一步最多一次文件调用加约 1 ms CPU，GUI 每圈睡眠，没活就让出，所以两者同时就绪的时间很短、交替各占一个 tick，谁也饿不死谁（真实的 CPU 份额与交错只能上板量，R5.3、R5.4、R5.6）。GUI 与前台之间只有 `ui_cell` 的短锁：GUI 的写和读都是不等的，前台的写等到拿到锁为止，持锁者只拷内存，所以 GUI 永远不被前台的文件调用挡住。Log、Monitor 升到 2 让日志和监控不被 GUI、前台饿着。Flash 启动任务低于卷任务，`MountFlash` 持 Flash 卷锁 3.3 至 8.3 s 期间插拔卡不受影响（SD 与 Flash 各一把卷锁）。
- **Storage、Background 同级 1**：时间片轮转，只拿 2 及以上的任务睡着时的空隙。Storage 现在只剩曲库扫描和 Flash 回收，扫描变慢是可接受的（用户看不到进度之外的延迟）；后台任务只拿 GUI 与前台睡着时的空隙（唱盘页上只分得约 7 至 13 %，作业慢是预期，不设饥饿兜底），不抢前台。后台作业在 Flash 卷就绪之前不碰 Flash 卷，所以不和 Flash 启动任务争 `MountFlash` 持的锁。Storage 持 SD 锁调目录读取时，前台要读标签就等那一次调用，持锁的 Storage 经优先级继承临时升到 2，等价于被“请”着先做完。
- **Boot 最低**：只在启动时创建任务。
- **定时器服务任务与卷任务同为 3、低于播放（4）**：`configTIMER_TASK_PRIORITY` 为 3，应用目前没有软件定时器（卷任务的消抖用等通知带超时，不用软件定时器），它一直阻塞、不占 CPU；引入软件定时器时回调须很短，否则复核这三个优先级。`FreeRTOSConfig.h` 的优先级相关配置不改。

启动顺序：Boot 先做内存准备（可选的 SDRAM 自检，做完才创建用到 SDRAM 的任务）；Background Task 在 GUI Task 之后创建，入口记下自己的句柄、让钩子投启动日志，然后进循环；Boot 的内存准备还包括曲库锁（`catalog_boot_init()`）、音频块池与两个信箱（`audio_reader_boot_init()`）、`ui_cell` 的全局锁（`ui_cell_boot_init()`，直接调，不放进注册表，Boot 栈核算见 [前台任务](foreground/README.md)）和注册表里各应用的 `boot_init`（`app_table_boot_init()`：音乐的四个界面数据格子、行缓存键表与前台流程状态），做完才创建用到它们的任务；Storage Task 先创建，运行到首次阻塞时已登记卷事件订阅；Audio Read Task 随后创建，运行到第一次等待（登记卷事件订阅、先看一次卷序号和请求信箱）；Volume Task 随后创建，入口先创建 Flash 启动任务再初始化 SD；Playback Task 晚于 Audio Read Task 创建；Foreground Task 晚于 Playback Task、早于 Monitor 和 GUI Task 创建（入口登记卷事件订阅、先调一圈所有应用的前台 step，所以晚于第一条需求或第一个卷事件启动也不会漏）；Playback Task 先于 GUI Task 创建，使命令 FIFO、状态信箱与回调注册先就绪。Boot 优先级最低，任何更高优先级的任务没阻塞时它不会继续，所以这些先后不是靠运气。Boot 只有 128 word 栈，装不下 SDRAM 自检，核算见 [卷任务](volume/README.md)“Boot 栈核算”。

## 约束

- 任务入口不得返回；
- Task 之间不能共享 FatFs 对象或绕过其资源所有者；播放任务不碰 FatFs，音频文件的打开、读取、关闭只在音频读取任务（ARC-12）；卷任务、音频读取任务、前台任务与 Storage Task 共用 Filesystem 的卷锁，卷任务不等订阅者；前台任务一步最多一次文件调用加约 1 ms CPU，不碰 Flash 卷（ARC-26）；
- ISR 只唤醒任务，不能把文件系统、日志格式化或设备初始化移入中断。

## 命名

任务入口和内部 Implementation 使用 `snake_case`；栈/优先级宏以 `APP_<TASK>_...` 命名。`APP/tasks` 是 APP 的私有分区，不因目录存在而形成面向其他 Module 的公开 Interface。
