# 音乐应用的 GUI 分区

本目录 `APP/apps/music/gui/` 是音乐应用的 GUI 编排，由 GUI Task 经注册表 [`APP/apps/app_table.c`](../../app_table.c) 的 `gui_init`、`gui_step` 两列调用（接入约定见 [APP/apps](../../README.md)），不是独立 Service。它做两件事：把带 `MUSIC_` 前缀的输入翻译成 [#71](https://github.com/immorcoding/mp3_project/issues/71) 公开合同的播放命令，把每圈读到的播放快照画出来；同时写 Queue 窗口与曲名的最新需求、读前台任务的结果（GUI 不等文件、不查询卷状态）。不持 playing 真值、不读写播放游标、不打开文件、不解码、不使用 LVGL（#72 A1–A3）。

## 私有 Interface

- `gui_music_init()`：复位窗口客户与快照归约，把播放键画成 PLAY、进度条为 0、唱盘归零；整块写入 GUI 一侧的两份结果副本（AXI 段，NOLOAD）。时间 Label 的初始 `0:00/--:--` 由 Service 在 Init 时写入。
- `gui_music_step(input)`：先发命令，再读一次快照归约，再读窗口与曲名结果、写最新需求，最后只对与上一圈不同的显示部分调用 `Service_GUI_*`（见“运行时路径”）。
- `gui_music_queue_window_*()`：窗口客户（纯 C）。每帧 `note_lead` 由滚动算出下一窗起点（可见首行之上留 `GUI_MUSIC_QUEUE_WINDOW_BACK_ROWS`（3）行给回滑，回到窗口顶部时一次退回这么多行，#157）；`request_due` / `request_index` / `request_sent` 决定要不要往窗口需求格写起点（格子里始终是“现在想要的起点”：滚动途中起点又变就写新的，不等上一份结果；起点又回到已显示的窗口就写回去，撤销上一个还没答的需求；起点没变不重写，前台任务会主动重新发窗）；`poll` 按本帧是否读到新窗口及其代次给出 APPLY（新窗口，或上次套用失败的重试，从 GUI 的窗口副本重来）或 CLEAR（代次 0 的空窗，且 Queue 正显示着一窗，一次）；`mark_applied` 记下已显示窗口的起点、条数与代次。**窗口不比需求序号**：窗口自带起点、代次和条数，又是整份拷贝，晚到也仍是列表上一段正确的内容（#200）。
- `gui_music_queue_rows()`：把窗口副本每行的 {曲目显示名 `Title`, 歌手 `Artist`} 按 `Length`（不超过容量 `MUSIC_WINDOW_MAX_ENTRIES`）整理成两个平行指针数组，APPLY 时交给 `Service_GUI_QueueApply()`；歌手为空串的行交出界面文案 `Unknown Artist`（GUI-25，定义在 `gui_music_text.h`，唱盘页歌手行复用），不是 `--`，窗口副本本身不改。窗口行只有显示文本，本分区不解析路径（GUI-9）。主机测试见 `Tests/music_gui`。
- `gui_music_text.h`：只含宏，集中定义取不到曲目元数据时的界面占位文案（目前是 `GUI_MUSIC_TEXT_UNKNOWN_ARTIST`，GUI-25），前台任务给出的窗口行与曲名内容不变，占位只在 GUI 侧替换；被 `gui_music_queue_rows.c` 与 `gui_music_title.c`（唱盘页歌手行）包含。
- `gui_music_transport_*()`：纯 C 快照归约（不含 LVGL，只包含 `playback_contract.h`），见下节。
- `gui_music_title_*()`：纯 C 曲名客户（不含 LVGL、FreeRTOS，只包含格子的类型头 `music_cells.h`），与 Queue 窗口客户并列，见“曲名”。

## 快照归约

`gui_music_transport` 不持 playing、游标与假进度；输入快照、最后发出的命令序号与已显示窗口代次，输出 `GUI_MusicTransportDecisionTypeDef`（#72 C1–C8、B5、B6）：

- `gui_music_transport_send()`：给命令编号并调 `playback_command_send()`，只有返回 `PLAYBACK_OK` 才记为最后发出（序号失败不占用）。
- `gui_music_transport_seek_press/drag/release/cancel()`：进度条手势（千分比，满量程 `GUI_MUSIC_TRANSPORT_SEEK_SCALE` = 1000，与 `SERVICE_GUI_SEEK_SCALE` 同单位），见下节“跳转”。
- `gui_music_transport_reduce()`：回显序号按有符号差判“未追上”；未追上时曲目身份、三态、停止原因与总时长冻结在最后一份已追上的快照，只有位置与两个计数照用。输出采信的三态、Queue 高亮（未追上时 `highlight_apply` 为假）、进度百分比（64 位计算并钳位）、位置与总时长、常驻停止短语（NULL 表示显示时间）、跳过增量、是否闪烁、唱盘转/归零，以及进度条可否响应（`seek_enabled`）、是否在拖（`seek_dragging`）与一次性的取消拖动事件（`seek_cancel`）。
- `gui_music_transport_withdraw_highlight()`：按最后一份已追上的快照与给定窗口代次套 C3 后三条。队列满撤回点行即时高亮（B6）与 APPLY 新窗口时都用它。
- 停止短语表是模块内静态表（合同枚举属 APP，Service 不得包含合同头）。
- `gui_music_queue_window_displayed_generation()`：APPLY 时 `mark_applied()` 记下的窗口代次，即归约的 `window_generation` 输入，也是 SELECT 携带的代次；未显示或被 CLEAR 后为 0。

## 跳转

拖动进度条的呈现归 `gui_music_transport`（GUI-10、GUI-17、#165）；状态只由快照与手势推出，不预测后端：

- **可否响应**：上一次归约采信的快照有当前曲、不在停止态且总时长非 0 时 `seek_enabled` 为真；`gui_music.c` 在它变化时调 `Service_GUI_SeekEnable()`。为假时 Service 吞掉按压，进度条不跟手、不置灰。
- **按下**（`seek_press`）：只在可响应时开始拖动，绑定上一次归约显示的曲目身份与总时长；总时长未知时什么也不做。
- **拖动中**：决定的时间与进度是预览值——`position_ms` = 比例 × 按下时的总时长，`duration_ms` = 按下时的总时长，样式与平时相同（`TimeApply` 照常写）。
- **换曲取消**：拖动中采信的曲目身份变化或变得不可跳转（停止、总时长归 0），本圈 `seek_cancel` 为真，`gui_music.c` 调 `Service_GUI_SeekCancel()` 并无条件重写进度条；之后的松手不发命令。触摸丢失（`seek_cancel()`）同样结束拖动、不发命令。
- **松手**（`seek_release`）：发 `SEEK{按下时的 Generation/Index, TargetMs = 比例 × 按下时的总时长}`，每次都发、不节流。发送成功记为在途跳转；队列满返回 `QUEUE_FULL`，`gui_music.c` 记 WARN，进度条回到快照（GUI-17）。
- **在途保持**：在途跳转的序号未被 `HandledCommandSeq` 追上、且采信的曲目仍是目标所属曲目时，位置与进度显示目标；连续松手只保持最后一次的目标。回显追上后按快照（落点）显示。
- 松手或触摸丢失的那一圈 `gui_music.c` 无条件 `ProgressApply`：Service 的进度条停在手指处，要改成目标或快照。

## 曲名

曲名区只显示三种内容（GUI-26）：名字；空白（已采信新身份、结果未到）；`--`（无当前曲，或有当前曲但取不到）。名字是前台任务答在曲名结果格里的曲目显示名 `Title`（有可用 TIT2 时是标题，否则是文件主名，STOR-15），本分区不解析路径；身份、空白与 `--` 按（代次, 位置）判定，与文本来源无关。歌手行随曲名：显示名字时为同一结果的歌手 `Artist`，空串时为界面文案 `Unknown Artist`（GUI-25，在本分区替换）；曲名为空白或 `--` 时歌手为空串。两者经同一次 `Service_GUI_TitleApply(title, artist)` 写入，不会出现新曲名配旧歌手。

- 输入是快照归约**已追上**的曲目身份：`GUI_MusicTransportDecisionTypeDef` 的 `has_track` / `generation` / `index`，与 Queue 高亮、唱盘归零同源；回显未追上时身份冻结，不写新需求。
- **需求带递增序号，结果带回所答的需求序号，只采信序号等于最新需求的结果**（曲名要比序号，窗口不比，#200）。`gui_music_title_poll(client, identity, reply)` 每帧一次：先比较身份，变化即采信并清空旧名（有当前曲留空并待写需求，无当前曲显示 `--` 不写）；再处理读到的新结果——序号等于最新写出的需求且应答代次非 0 返回 `ACCEPT`（显示名字），代次 0 返回 `DISCARD` 并显示 `--`，序号落后（身份已变、需求已被覆盖）返回 `DISCARD`（不显示旧名）。**不再有“在途期间不能写新需求”的限制**：身份一变就写新序号的需求（`request_due` / `request` / `request_sent`），旧需求的结果到达时序号落后被丢弃，所以连按 NEXT 只采信最后一首；坏文件跳过时快照位置已前进到候选，按身份变化处理。写需求拿不到锁（`ui_cell_try_write()` 返回假）时保持“待写”，下一帧同一序号重试（失败不占序号）。
- 同一序号的结果可以多次到达：曲库发布后前台主动重答一次（先到的代次 0 显示 `--`，后到的名字替换它），GUI 不用重写需求。
- 冷启动：快照代次 0 时以代次 0（“当前”）写需求，前台按实际代次 G 答。之后快照代次 0 → G 且位置不变：已显示名字的结果代次等于 G 就保留名字、不重写（不因唱盘这次归零闪空白，需求序号不变）；代次 0 的需求仍在途则等结果，结果代次等于 G 照常采信，否则（含代次 0）丢弃并按 G 重写；其余情况按普通身份变化清空并重写。
- `gui_music_title_text(client, action, title, artist, &text)` 在 poll 之后决定本帧要不要 Apply、Apply 哪一对：`ACCEPT` 一律交出 {结果副本的 `Title`, `Artist` 或 `Unknown Artist`}；否则只在显示与上次交出的不同时交出 {`""`, `""`}（空白）或 {`"--"`, `""`}。
- `gui_music.c` 执行：`ui_cell_read_if_changed(曲名结果格)` 读到新结果就连同 GUI 副本交给 `poll`；`request_due` 为真时 `ui_cell_try_write(曲名需求格)`，成功才 `request_sent`；`gui_music_title_text()` 返回真时调 `Service_GUI_TitleApply(title, artist)`。副本是 GUI 自己的，Service 拷完文本后无需“写回 IDLE”。
- 时序证据：主机测试 `Tests/music_gui` 覆盖以上全部规则（每次 Apply 同时断言曲名与歌手）；模拟器场景 `music_artist`、`music_title_play_start`、`music_title_switch`、`music_title_next_burst`、`music_title_bad_file`、`music_title_unavailable` 用假前台的应答延迟与“取不到”开关拍出对应画面（[#120](https://github.com/immorcoding/mp3_project/issues/120)，假后端保持“一次答一个”的节奏，所以拍不到“新需求覆盖旧需求”，那由 `Tests/music_foreground` 和第 5 次上板负责）。真机前台应答时延与 GUI 刷新节拍的组合进板级验收（R5.1：曲名 ≤ 200 ms）。

## 编译期依赖

- `Service_GUI_*` 公开 Interface；
- `APP/tasks/playback/playback_contract.h`（GUI 中只有本分区包含）；
- `APP/apps/music/music_cells.h`（四个界面数据格子的类型与对象）与 `APP/ui_cell/ui_cell.h`（短锁模块，只在 GUI 与任务之间交接，ARC-24）；
- `APP/apps/music/music_display_stats.h`（显示延迟汇总的样本入口，Debug）与 `music_hook_pageturn.h`（翻页注入钩子，默认关闭）；
- FreeRTOS `xTaskGetTickCount()`（只量延迟，不等待）；
- `Service_Log_Post()`：命令队列满时记日志。

不读写播放游标：游标只有播放任务一个写者。

## 运行时路径

~~~text
ConsumeInput
  -> gui_music_step
       -> PREVIOUS / PLAY_PAUSE / NEXT：发 PREV / TOGGLE / NEXT（连按 N 次发 N 条，不合并）
       -> QUEUE_SELECT：发 SELECT{已显示窗口代次, 位置}；Service 已即时高亮本行
            -> 队列满：丢弃、记日志，按最后一份已追上的快照重套本窗，撤回即时高亮（B6）
       -> SEEK_PRESS / SEEK_DRAG：交给跳转归约（按下绑定曲目与总时长、拖动更新比例）
       -> SEEK_RELEASE：发 SEEK{按下时的曲目, 比例 × 按下时的总时长}；队列满记日志；本圈无条件重写进度条
       -> SEEK_CANCEL：结束拖动、不发命令；本圈无条件重写进度条
       -> playback_snapshot_read -> gui_music_transport_reduce
       -> 窗口：读窗口结果格（变了才读）-> APPLY（按已追上快照套高亮）/ CLEAR（代次 0 的空窗，只清空 Queue，C10）；
          再按滚动算出的起点写窗口需求格（不等，拿不到锁下一帧再试）
       -> 曲名与歌手：读曲名结果格（变了才读）按需求序号 ACCEPT（TitleApply 名字与歌手）/ DISCARD；身份变了写曲名需求格；
          空白或 `--` 变化时 TitleApply（歌手为空）
       -> 回显已追上、窗口仍显示：高亮变化或点行后用 GUI 的窗口副本重套 QueueApply（不自动滚动）
       -> 与上一圈比较：TransportApply(三态 == PLAYING) / SeekEnable(可否响应) / 取消拖动时 SeekCancel / ProgressApply / TimeApply
       -> 三态变化或归零事件：VinylApply(playing, reset_angle)
       -> 常驻短语变化：StatusApply(短语或 NULL)；跳过增量 > 0：SkipNoticeApply(k)；停止次数增加：StatusFlash()
~~~

- 唱盘（GUI-20）：显示的三态为 PLAYING 时转，否则停在原角度；采信的曲目身份变化、快照 `TrackStartCount` 变化（含同曲重选）或进入停止时归零，跳转（含跳到 0）不归零，暂停中跳转保持静止。归零是一次性事件，出现的那一圈必须调用，不与上一圈比较。曲库代次从 0（开启尚未确认）变为非 0 也算身份变化，开播后约在开启应答时多归零一次，此时位置接近 0。
- 状态行（C6、C7、GUI-19）：常驻短语与上一圈不同时调用 `Service_GUI_StatusApply()`（短语来自模块内静态表，按指针比较；NULL 回到时间）；跳过增量与闪烁是一次性事件，出现的那一圈调用 `Service_GUI_SkipNoticeApply(k)`、`Service_GUI_StatusFlash()`，不与上一圈比较。同一圈先调 `StatusApply` 再调 `SkipNoticeApply`，所以整轮坏文件进入 ALL_BAD 那一圈新增的跳过被常驻短语挡掉。显示优先级、2 s 提示计时与闪烁动画都在 Service 内，本分区不使用 LVGL。
- 三键与进度条从不置灰（C9）。
