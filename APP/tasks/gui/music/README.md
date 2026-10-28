# GUI Task 音乐分区

`music/` 是 GUI Task 的音乐播放器编排，不是独立 Service。它做两件事：把带 `MUSIC_` 前缀的输入翻译成 [#71](https://github.com/immorcoding/mp3_project/issues/71) 公开合同的播放命令，把每圈读到的播放快照画出来；同时推进 Queue 窗口协议与曲名请求。不持 playing 真值、不读写播放游标、不打开文件、不解码、不使用 LVGL（#72 A1–A3）。

## 私有 Interface

- `gui_music_init()`：复位窗口客户与快照归约，把播放键画成 PLAY、进度条为 0、唱盘归零。时间 Label 的初始 `0:00/--:--` 由 Service 在 Init 时写入。
- `gui_music_step(input)`：先发命令，再读一次快照归约，再跑窗口协议，最后只对与上一圈不同的显示部分调用 `Service_GUI_*`（见“运行时路径”）。
- `gui_music_queue_window_*()`：listbuffer 窗口 request / APPLY / CLEAR 时机；APPLY 时记下已显示窗口的曲库代次。
- `gui_music_queue_rows()`：把 READY 窗口槽每行的 {曲目显示名 `Title`, 歌手 `Artist`} 按 `Length`（不超过容量）整理成两个平行指针数组，APPLY 时原样交给 `Service_GUI_QueueApply()`；歌手取不到的行是空串，不替换成 `--`（GUI-25）。窗口行只有显示文本，本分区不解析路径（GUI-9）。主机测试见 `Tests/gui_task`。
- `gui_music_transport_*()`：纯 C 快照归约（不含 LVGL，只包含 `playback_contract.h`），见下节。
- `gui_music_title_*()`：纯 C 曲名客户（不含 LVGL，不包含 Storage 头），与 Queue 窗口客户并列，见“曲名”。

## 快照归约

`gui_music_transport` 不持 playing、游标与假进度；输入快照、最后发出的命令序号与已显示窗口代次，输出 `GUI_MusicTransportDecisionTypeDef`（#72 C1–C8、B5、B6）：

- `gui_music_transport_send()`：给命令编号并调 `playback_command_send()`，只有返回 `PLAYBACK_OK` 才记为最后发出（序号失败不占用）。
- `gui_music_transport_reduce()`：回显序号按有符号差判“未追上”；未追上时曲目身份、三态、停止原因与总时长冻结在最后一份已追上的快照，只有位置与两个计数照用。输出采信的三态、Queue 高亮（未追上时 `highlight_apply` 为假）、进度百分比（64 位计算并钳位）、位置与总时长、常驻停止短语（NULL 表示显示时间）、跳过增量、是否闪烁、唱盘转/归零。
- `gui_music_transport_withdraw_highlight()`：按最后一份已追上的快照与给定窗口代次套 C3 后三条。队列满撤回点行即时高亮（B6）与 APPLY 新窗口时都用它。
- 停止短语表是模块内静态表（合同枚举属 APP，Service 不得包含合同头）。
- `gui_music_queue_window_displayed_generation()`：APPLY 时 `mark_applied()` 记下的窗口代次，即归约的 `window_generation` 输入，也是 SELECT 携带的代次；未显示或被 CLEAR 后为 0。

## 曲名

曲名区只显示三种内容（GUI-21）：名字；空白（已采信新身份、应答未到）；`--`（无当前曲，或有当前曲但取不到）。名字是名称槽（`storage_namebuffer`，STOR-15）应答的曲目显示名 `Title`（有可用 TIT2 时是标题，否则是文件主名），由 Storage 给出，本分区不解析路径；身份、空白与 `--` 按（代次, 位置）判定，与文本来源无关。应答中的歌手 `Artist` 0.8.0 不显示（唱盘页歌手行是 0.8.2）。

- 输入是快照归约**已追上**的曲目身份：`GUI_MusicTransportDecisionTypeDef` 的 `has_track` / `generation` / `index`，与 Queue 高亮、唱盘归零同源；回显未追上时身份冻结，不发新请求。
- `gui_music_title_poll(client, identity, status, reply_generation)` 每圈一次：先比较身份，变化即采信并清空旧名（有当前曲留空并待请求，`HasTrack = false` 显示 `--` 不请求）；再按槽状态推进——`IDLE` 且待请求时返回 `REQUEST`，`READY` 时以**请求时的身份**判断：身份未变且应答代次非 0 返回 `ACCEPT`（显示名字），代次 0 返回 `DISCARD` 并显示 `--`，身份已变返回 `DISCARD`（不显示旧名，下一圈按最新身份重请求）。槽忙（`PENDING`）时身份再变也不撤销在途请求：等它 `READY`、丢弃、再按最新身份请求，因此连按 NEXT 只采信最后一首；坏文件跳过时快照位置已前进到候选，按身份变化处理，候选名立即被请求。
- 冷启动：快照代次 0 时以代次 0（“当前”）请求，Storage 按实际代次 G 应答。之后快照代次 0 → G 且位置不变：已显示名字的应答代次等于 G 就保留名字、不重请求（不因唱盘这次归零闪空白）；代次 0 的请求仍在途则等应答，应答代次等于 G 照常采信，否则（含代次 0）丢弃并按 G 重请求；其余情况按普通身份变化清空并重请求。
- `gui_music.c` 执行动作：`REQUEST` 调 `storage_namebuffer_request(位置, 代次)`，被拒时 `gui_music_title_request_failed()` 下一圈重试；`ACCEPT` 先 `Service_GUI_TitleApply(Title)` 再写回 `IDLE`；`DISCARD` 直接写回 `IDLE`。空白与 `--` 只在与上一圈不同时调用 `TitleApply("")` / `TitleApply("--")`。
- 先读槽 `Status`，为 `READY` 时才读应答代次与名字：`READY` 期间 Storage 不再写槽。
- 时序证据：主机测试 `Tests/gui_task` 覆盖以上全部规则；模拟器场景 `music_title_play_start`、`music_title_switch`、`music_title_next_burst`、`music_title_bad_file`、`music_title_unavailable` 用假 Storage 的应答延迟与“取不到”开关拍出对应画面（[#120](https://github.com/immorcoding/mp3_project/issues/120)）。真机 Storage 应答时延与 GUI 刷新节拍的组合进板级验收。

## 编译期依赖

- `Service_GUI_*` 公开 Interface；
- `APP/tasks/playback/playback_contract.h`（GUI 中只有本分区包含）；
- `storage_listbuffer_*()`、`storage_namebuffer_*()` 与 `storage_task_sd_is_ready/mounted()`（只允许出现在本分区）；
- `Service_Log_Post()`：命令队列满时记日志。

不读写播放游标：游标只有播放任务一个写者。

## 运行时路径

~~~text
ConsumeInput
  -> gui_music_step
       -> PREVIOUS / PLAY_PAUSE / NEXT：发 PREV / TOGGLE / NEXT（连按 N 次发 N 条，不合并）
       -> QUEUE_SELECT：发 SELECT{已显示窗口代次, 位置}；Service 已即时高亮本行
            -> 队列满：丢弃、记日志，按最后一份已追上的快照重套本窗，撤回即时高亮（B6）
       -> SEEK：不发命令，本圈按快照重写一次进度条（回位，B3）
       -> playback_snapshot_read -> gui_music_transport_reduce
       -> 窗口 REQUEST / APPLY（按已追上快照套高亮）/ CLEAR（只清空 Queue，C10）
       -> 曲名：按已追上身份 REQUEST / ACCEPT（TitleApply 名字）/ DISCARD；空白或 `--` 变化时 TitleApply
       -> 回显已追上、窗口仍显示且 IDLE：高亮变化或点行后重套 QueueApply（不自动滚动）
       -> 与上一圈比较：TransportApply(三态 == PLAYING) / ProgressApply / TimeApply
       -> 三态变化或归零事件：VinylApply(playing, reset_angle)
       -> 常驻短语变化：StatusApply(短语或 NULL)；跳过增量 > 0：SkipNoticeApply(k)；停止次数增加：StatusFlash()
~~~

- 唱盘（C8）：显示的三态为 PLAYING 时转，否则停在原角度；曲目身份变化或位置变小时归零。归零是一次性事件，出现的那一圈必须调用，不与上一圈比较。曲库代次从 0（开启尚未确认）变为非 0 也算身份变化，开播后约在开启应答时多归零一次，此时位置接近 0。
- 状态行（C6、C7、GUI-19）：常驻短语与上一圈不同时调用 `Service_GUI_StatusApply()`（短语来自模块内静态表，按指针比较；NULL 回到时间）；跳过增量与闪烁是一次性事件，出现的那一圈调用 `Service_GUI_SkipNoticeApply(k)`、`Service_GUI_StatusFlash()`，不与上一圈比较。同一圈先调 `StatusApply` 再调 `SkipNoticeApply`，所以整轮坏文件进入 ALL_BAD 那一圈新增的跳过被常驻短语挡掉。显示优先级、2 s 提示计时与闪烁动画都在 Service 内，本分区不使用 LVGL。
- 三键与进度条从不置灰（C9）。
