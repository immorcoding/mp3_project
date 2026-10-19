# GUI Task 音乐分区

`music/` 是 GUI Task 的音乐播放器编排，不是独立 Service。它消费带 `MUSIC_` 前缀的输入命令，推进 Queue 窗口协议，并持有 paused/playing 与假进度。不打开文件、不解码。

## 私有 Interface

- `gui_music_init()`：复位窗口客户、playing 与假进度，并把播放键画成 PLAY、进度条为 0。
- `gui_music_step(input)`：先分发本分区命令，再跑窗口 poll，必要时刷新 Queue 高亮、PLAY/PAUSE 与进度条。
- `gui_music_queue_window_*()`：listbuffer 窗口 request / APPLY / CLEAR 时机。
- `gui_music_transport_*()`：纯 C 快照归约（不含 LVGL，只包含 `playback_contract.h`），见下节“快照归约”。**当前 `gui_music.c` 尚未接入**（#84 接线）：过渡期 `gui_music.c` 暂自持旧的 playing 与假进度，#84 删除。
- 自动走表未实现：等 Playback 后台用解码器时间对接，见 `gui_music_step` 注释框。
- Now Playing 时间 Label 已由 `view/` 创建，本分区尚未填时间。
- 唱盘旋转：playing 变化时 `Service_GUI_VinylApply(playing, false)`；游标变化（选曲含选中同一首、上一首/下一首，与进度归零同条件）时 `VinylApply(playing, true)` 归零后按 playing 续转；游标操作失败不归零；CLEAR 时 `VinylApply(false, true)` 停转归零。拖进度条不碰唱盘。

## 快照归约

`gui_music_transport` 不持 playing、游标与假进度；输入快照、最后发出的命令序号与已显示窗口代次，输出 `GUI_MusicTransportDecisionTypeDef`（#72 C1–C8、B5、B6）：

- `gui_music_transport_send()`：给命令编号并调 `playback_command_send()`，只有返回 `PLAYBACK_OK` 才记为最后发出（序号失败不占用）。
- `gui_music_transport_reduce()`：回显序号按有符号差判“未追上”；未追上时曲目身份、三态、停止原因与总时长冻结在最后一份已追上的快照，只有位置与两个计数照用。输出采信的三态、Queue 高亮（未追上时 `highlight_apply` 为假）、进度百分比（64 位计算并钳位）、位置与总时长、常驻停止短语（NULL 表示显示时间）、跳过增量、是否闪烁、唱盘转/归零。
- `gui_music_transport_withdraw_highlight()`：队列满撤回点行即时高亮时，按最后一份已追上的快照套 C3 后三条（B6）。
- 停止短语表是模块内静态表（合同枚举属 APP，Service 不得包含合同头）。
- `gui_music_queue_window_displayed_generation()`：APPLY 时 `mark_applied()` 记下的窗口代次，即归约的 `window_generation` 输入；未显示或被 CLEAR 后为 0。

## 编译期依赖

- `Service_GUI_*` 公开 Interface；
- `storage_listbuffer_*()` 与 `storage_playback_cursor_*()`（只允许出现在本分区）。

## 运行时路径

~~~text
ConsumeInput
  -> gui_music_step
       -> QUEUE_SELECT：cursor_set（假切歌），进度归零
       -> PREVIOUS / NEXT：游标环形步进，进度归零
       -> PLAY_PAUSE：有当前曲才翻转 playing
       -> SEEK：记下百分比，不改 playing；无当前曲则忽略并写回已存进度
       -> 窗口 REQUEST / APPLY / CLEAR（CLEAR 强制 paused 且进度归零）
       -> IDLE 且仍展示时按新游标 QueueApply
       -> TransportApply(playing) / ProgressApply(percent)
       -> playing 变化或切歌/清空：VinylApply(playing, reset_angle)
~~~

点 Queue 行仍由 Service 立刻改行样式。Playback 打开/预开只留注释。拖动进度条不改 playing。
