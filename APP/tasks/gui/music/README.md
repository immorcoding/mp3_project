# GUI Task 音乐分区

`music/` 是 GUI Task 的音乐播放器编排，不是独立 Service。它做两件事：把带 `MUSIC_` 前缀的输入翻译成 [#71](https://github.com/immorcoding/mp3_project/issues/71) 公开合同的播放命令，把每圈读到的播放快照画出来；同时推进 Queue 窗口协议。不持 playing 真值、不读写播放游标、不打开文件、不解码、不使用 LVGL（#72 A1–A3）。

## 私有 Interface

- `gui_music_init()`：复位窗口客户与快照归约，把播放键画成 PLAY、进度条为 0、唱盘归零。时间 Label 的初始 `0:00/--:--` 由 Service 在 Init 时写入。
- `gui_music_step(input)`：先发命令，再读一次快照归约，再跑窗口协议，最后只对与上一圈不同的显示部分调用 `Service_GUI_*`（见“运行时路径”）。
- `gui_music_queue_window_*()`：listbuffer 窗口 request / APPLY / CLEAR 时机；APPLY 时记下已显示窗口的曲库代次。
- `gui_music_transport_*()`：纯 C 快照归约（不含 LVGL，只包含 `playback_contract.h`），见下节。

## 快照归约

`gui_music_transport` 不持 playing、游标与假进度；输入快照、最后发出的命令序号与已显示窗口代次，输出 `GUI_MusicTransportDecisionTypeDef`（#72 C1–C8、B5、B6）：

- `gui_music_transport_send()`：给命令编号并调 `playback_command_send()`，只有返回 `PLAYBACK_OK` 才记为最后发出（序号失败不占用）。
- `gui_music_transport_reduce()`：回显序号按有符号差判“未追上”；未追上时曲目身份、三态、停止原因与总时长冻结在最后一份已追上的快照，只有位置与两个计数照用。输出采信的三态、Queue 高亮（未追上时 `highlight_apply` 为假）、进度百分比（64 位计算并钳位）、位置与总时长、常驻停止短语（NULL 表示显示时间）、跳过增量、是否闪烁、唱盘转/归零。
- `gui_music_transport_withdraw_highlight()`：按最后一份已追上的快照与给定窗口代次套 C3 后三条。队列满撤回点行即时高亮（B6）与 APPLY 新窗口时都用它。
- 停止短语表是模块内静态表（合同枚举属 APP，Service 不得包含合同头）。
- `gui_music_queue_window_displayed_generation()`：APPLY 时 `mark_applied()` 记下的窗口代次，即归约的 `window_generation` 输入，也是 SELECT 携带的代次；未显示或被 CLEAR 后为 0。

## 编译期依赖

- `Service_GUI_*` 公开 Interface；
- `APP/tasks/playback/playback_contract.h`（GUI 中只有本分区包含）；
- `storage_listbuffer_*()` 与 `storage_task_sd_is_ready/mounted()`（只允许出现在本分区）；
- `Service_Log_Post()`：命令队列满时记日志。

不包含 `storage_playback_cursor.h`：游标只有播放任务一个写者。

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
       -> 回显已追上、窗口仍显示且 IDLE：高亮变化或点行后重套 QueueApply（不自动滚动）
       -> 与上一圈比较：TransportApply(三态 == PLAYING) / ProgressApply / TimeApply
       -> 三态变化或归零事件：VinylApply(playing, reset_angle)
~~~

- 唱盘（C8）：显示的三态为 PLAYING 时转，否则停在原角度；曲目身份变化或位置变小时归零。归零是一次性事件，出现的那一圈必须调用，不与上一圈比较。曲库代次从 0（开启尚未确认）变为非 0 也算身份变化，开播后约在开启应答时多归零一次，此时位置接近 0。
- 状态行（C6、C7）：归约已输出常驻短语、跳过增量与闪烁，本分区暂不消费；接 `Service_GUI_StatusApply/SkipNoticeApply/StatusFlash` 由状态行票（#86）完成，同样在产生的这一圈消费。
- 三键与进度条从不置灰（C9）。
