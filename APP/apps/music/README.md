# 音乐应用

音乐是注册表 `APP/apps/app_table.c` 里的第一个应用。目前这个目录只有 GUI 分区，其他部分仍在旧位置：

- `gui/`：GUI 分区，见 [gui/README.md](gui/README.md)。入口 `gui_music_init()`、`gui_music_step()` 在注册表里登记为 `gui_init`、`gui_step`。
- 播放与音频读取：`APP/tasks/playback/`，音乐专用任务，留在 `APP/tasks/`。
- 曲库与窗口槽、名称槽：`APP/tasks/storage/`（0.10.0 逐步搬走，见 [#202](https://github.com/immorcoding/mp3_project/issues/202) K 节）。
- LVGL 一半：`Service/gui/main/` 与 `view/`。

行为、旧三态与 Storage 窗口槽/名称槽的用法在这张票里完全没变；后续票把前台流程、界面数据和后台作业逐步搬进本目录。
