# GUI Task 音乐分区

`music/` 是 GUI Task 的音乐播放器编排，不是独立 Service。它消费带 `MUSIC_` 前缀的输入命令，推进 Queue 窗口协议，并持有 paused/playing。不打开文件、不解码。

## 私有 Interface

- `gui_music_init()`：复位窗口客户与 playing，并把播放键画成 PLAY。
- `gui_music_step(input)`：先分发本分区命令，再跑窗口 poll，必要时刷新 Queue 高亮与 PLAY/PAUSE。
- `gui_music_queue_window_*()`：listbuffer 窗口 request / APPLY / CLEAR 时机。
- `gui_music_transport_*()`：仅 playing 标志；无当前曲不能开播；CLEAR 强制 paused。

## 编译期依赖

- `Service_GUI_*` 公开 Interface；
- `storage_listbuffer_*()` 与 `storage_playback_cursor_*()`（只允许出现在本分区）。

## 运行时路径

~~~text
ConsumeInput
  -> gui_music_step
       -> QUEUE_SELECT：cursor_set（假切歌）
       -> PREVIOUS / NEXT：游标环形步进
       -> PLAY_PAUSE：有当前曲才翻转 playing
       -> 窗口 REQUEST / APPLY / CLEAR
       -> IDLE 且仍展示时按新游标 QueueApply
       -> TransportApply(playing)
~~~

点 Queue 行仍由 Service 立刻改行样式。Playback 打开/预开只留注释。
