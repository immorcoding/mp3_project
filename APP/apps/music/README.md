# 音乐应用

音乐是注册表 `APP/apps/app_table.c` 里的第一个应用：一行 `{name "music", boot_init, gui_init, gui_step, foreground_step}`。0.10.0 前台一次切（[#218](https://github.com/immorcoding/mp3_project/issues/218)）之后，GUI 写最新需求、前台任务读文件并发布界面数据，GUI 不等文件、不查询卷状态。

## 目录

- `gui/`：GUI 分区，见 [gui/README.md](gui/README.md)。入口 `gui_music_init()`、`gui_music_step()` 在注册表里登记为 `gui_init`、`gui_step`。窗口客户、曲名客户、快照归约、Queue 行整理都是纯 C，可主机测试（`Tests/music_gui`）。
- `foreground/`：前台流程，见下节。入口 `music_foreground_step()` 登记为 `foreground_step`，`music_foreground_boot_init()` 经 `music_boot_init()` 登记为 `boot_init`。
- `catalog/`：曲库（SD `Music/` 的路径事实表、播放列表、曲目显示名与标签文本、曲目打开、读取诊断），见 [catalog/README.md](catalog/README.md)。一份表、模块内一把短锁，发布时比对卷序号，读者经 `catalog_read_track()` 一次读全；扫描是后台任务里的单步作业（`catalog_scan_step()`，经 `music_background_step()` 注册进注册表的 `background_step`，#220），发布成功后叫醒前台任务一次。
- 界面数据格子 `music_cells.h` / `music_cells.c`：曲名需求、曲名结果、窗口需求、窗口结果四个 `ui_cell` 格子的类型与存储（AXI 新段 `.app_axi_ram`，Boot 里整块写入）。需求格 GUI 写、前台任务读（叫醒函数 `foreground_task_wake`），结果格前台任务写、GUI 读（不叫醒）。
- `music_config.h`：窗口行数（12）、行缓存行数（64）、`SERVICE_BUSY` 重试间隔（10 ms，不设次数上限）与日志节拍（第 1 次和每满 100 次一行）、链接段名、显示延迟汇总的开关与节拍、翻页注入钩子的开关。
- `music_display_stats.*`：显示延迟汇总（曲名、新窗口、拔卡后 Queue 清空），走持锁统计的开关（Debug 开、Release 关），见下节“证据出口”。
- `music_hook_pageturn.*`：翻页注入测试钩子（R5.4），默认关闭、只允许 Debug，见 [前台任务](../../tasks/foreground/README.md)“翻页注入测试钩子”。
- `music_app.*`：Boot 入口 `music_boot_init()`（初始化四个格子、清零行缓存键表与前台流程状态）。
- 播放与音频读取：`APP/tasks/playback/`、`APP/tasks/audio_reader/`，音乐专用任务，留在 `APP/tasks/`。
- LVGL 一半：`Service/gui/main/` 与 `view/`，属于 Service 层。

## 前台流程（`foreground/`）

`music_foreground_step()`（见 `music_foreground.h`）每步先看曲名需求，有就处理它：命中行缓存不读文件，否则读一首的标签（一次文件调用，`catalog_track_text_read()`），把结果带着所答的需求序号发布到曲名结果格；本步没有文件调用时再最多读窗口的一行（命中缓存的行不读文件，可以一步拼进好几行），一窗凑齐（满 12 行或到列表末尾）才整份发布；最后重新看一次需求。一步最多一次文件调用加约 1 ms CPU，不碰 Flash 卷（ARC-26）。

- **覆盖**：新需求覆盖旧需求。窗口起点变了就丢掉没拼完的从新起点重拼，已读的行靠行缓存不重读；曲名只答最新的一次。
- **行缓存**（`music_row_cache.*`）：64 行，键是（曲库代次，位置），取位 `位置 mod 64`，曲名需求和窗口行共用；只缓存结果稳定的行（取到标签、无 ID3v2、帧缺失、解码失败、超读取上限）；打开失败、读取错误、`SERVICE_BUSY` 和卷不可用（卡不在）都不缓存，否则瞬时错误会让这首歌在换卡前一直显示文件主名；代次不符视为未命中，不主动清；键表启动时清零。
- **代次变化发空窗**：曲库代次变了（重扫、拔卡、换卡）或 SD 不在时发布代次 0 的空窗，GUI 据此清空 Queue；曲名在曲库不可用时答代次 0。
- **发布后重新发窗**：GUI 不会重写没变的需求，所以曲库发布后（`catalog.c` 叫醒前台一次）和卷就绪后，前台主动按最新需求用新代次重新拼窗、重新答曲名（曲名带回同一个需求序号）。
- **`SERVICE_BUSY`**（文件槽用尽，瞬时的）：不算文件错误，这一行或这首**一律**过约 10 ms 重试，不设次数上限（ARC-27，spec“过约 10 ms 重试，不算失败”），期间不显示回退名；日志 WARN `MUSIC: SERVICE_BUSY pos=<位置> n=<连续第几次>` 只在第 1 次和每满 100 次各一行，不刷屏（计入消息池核算）。终止靠卷事件、曲库代次变化（窗口以空窗收尾、曲名答代次 0）或新需求。
- **卷不可用（拔卡的正常竞态）不是文件错误**：窗口行读到 `NO_MEDIA`/`NOT_READY`/`INVALID_HANDLE` 时**不填这一行、不发布**，已发布的窗口保持不变（spec E“暂时不能读时窗口不变”），这一窗挂起、睡下等卷事件或新需求；卷消失时曲库代次读作 0，下一步以代次 0 的空窗收尾，卡回来重新发布后用新代次重拼。不会把带空行的整窗发出去。曲名读到卷不可用答代次 0（GUI 显示 `--`），不缓存。
- **定位符失效钩子**：`AUDIO_READER_HOOK_LOCATOR_MISS` 打开的构建里，前台读标签前也把定位符清零，走“退回按路径打开”（R5.5：前台回退按路径打开时切歌不显示 `Read timeout`）。
- **曲名与歌手的保留语义**：GUI 只见整窗（STOR-16）；曲名只显示名字、空白或 `--`，切歌清空旧名（GUI-26）；标签失败回退文件主名（STOR-15）；曲名需求的代次 0 表示当前；窗口行不含路径。

**内存放置**（ARC-28）：前台流程的缓冲与状态都在 `.apps_sdram`——行缓存的行内容与键表、窗口组装缓冲、曲名组装区、曲名读取用的临时一行（504 B，不放前台任务的栈，spec G“step 里不放大局部变量”）、曲库读全结果、各状态变量；DTCM 不放应用状态。`boot_init` 整块写入后才使用。

文件数：前台一步最多一个，不跨步持有（读完即关）。SD 的 4 个文件槽：音频流 1、前台 1、曲库扫描（只开目录，不开文件）——音频流、前台、曲库扫描同时进行时最多同时打开 2 个文件，不会用尽（`SERVICE_BUSY` 在 R5 和 R6.10 里核对不出现）。

## 证据出口（DIAG-6、DIAG-7）

- **显示延迟汇总**（Debug 开、Release 关，`MUSIC_DISPLAY_STATS_ENABLE`）：延迟在固件里算好再输出，用需求序号算——曲名：GUI 写曲名需求（带递增序号）到 GUI 第一次看到所答序号等于它的结果；新窗口：GUI 写窗口需求到 GUI 套用起点等于该需求的窗口；拔卡后 Queue 清空：卡检测边沿（卷任务记下的第一个边沿时刻）到 GUI 套用代次 0 空窗清空 Queue。**每个需求只在第一次显示其结果时计一次延迟**：曲库发布后前台对同一需求序号的重答（先到的代次 0 显示 `--`，后到的名字替换它）不再计入，所以扫描时间不会算进曲名延迟；冷启动时首个需求的第一次应答（往往是代次 0 的 `--`）会计入，它很快。样本由 GUI 一侧只加计数（不拿锁、不投日志），汇总由前台任务每圈投，至多每 10 s 一行、没有新样本不投。行：`I (<tick>) DISPLAY: title n=<本节拍样本数> max=<本节拍最大 ms>ms top=<开机以来最大 ms>ms win n=… max=…ms top=…ms clear n=… max=…ms top=…ms`（数字截在 99999，样本数截在 9999，没有样本的一类也列出）。R5.1 读 `title` 和 `win` 的 `max`/`top`，R5.2 读 `clear`。
- **`ui_cell` 持锁统计**：`I (<tick>) UICELL: n=… hold=…us gui=… busy=… pct=…`，见 [ui_cell](../../ui_cell/README.md)。R5.3 读它。
- **翻页注入钩子**：见 [前台任务](../../tasks/foreground/README.md)。R5.4 读它。
- **前台 step 耗时**：`FOREGROUND:` 行，见 [前台任务](../../tasks/foreground/README.md)。R5.6 读它。

## 编译期依赖

- `APP/ui_cell/`（格子）、`APP/tasks/foreground/foreground_task.h`（叫醒函数与 `FOREGROUND_WAIT_FOREVER`）、`APP/tasks/volume/volume_event.h`（卷边沿时刻）、`APP/tasks/audio_reader/audio_reader_block.h`（块池就绪数；它再导出配置宏，水位和定位符钩子开关经它取，不直接包含 `audio_reader_config.h`，STY-3，只有翻页钩子和定位符钩子用）：应用可以包含任务的公开合同（ARC-23）；
- 曲库 `catalog/`；Filesystem Service 的失败分类函数；Service Log。

行为、窗口与曲名的显示规则不变：Queue 窗口与曲名的用户可见结果由 GUI 模拟器的 42 个场景守住（`.expected` 一个都没动）。
