# GUI Task 音乐分区

`music/` 是 GUI Task 的音乐播放器编排，不是独立 Service。它消费带 `MUSIC_` 前缀的输入命令，推进 Queue 窗口协议，并持有 paused/playing 与假进度。不打开文件、不解码。

## 私有 Interface

- `gui_music_init()`：复位窗口客户、playing 与假进度，并把播放键画成 PLAY、进度条为 0。
- `gui_music_step(input)`：先分发本分区命令，再跑窗口 poll，必要时刷新 Queue 高亮、PLAY/PAUSE 与进度条。
- `gui_music_queue_window_*()`：listbuffer 窗口 request / APPLY / CLEAR 时机。
- `gui_music_transport_*()`：playing 与 0..100 进度；无当前曲不能开播；CLEAR 强制 paused 且进度归零。
- 自动走表未实现：等 Playback 后台用解码器时间对接，见 `gui_music_step` 注释框。
- Now Playing 时间 Label 已由 `view/` 创建，本分区尚未填时间。假唱盘第一帧见 `docs/shape/gui.md` 10.5.4，旋转尚未接线。

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
~~~

点 Queue 行仍由 Service 立刻改行样式。Playback 打开/预开只留注释。拖动进度条不改 playing。
