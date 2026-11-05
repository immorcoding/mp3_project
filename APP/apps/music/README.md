# 音乐应用

音乐是注册表 `APP/apps/app_table.c` 里的第一个应用。目前这个目录有 GUI 分区和曲库，其他部分仍在旧位置：

- `gui/`：GUI 分区，见 [gui/README.md](gui/README.md)。入口 `gui_music_init()`、`gui_music_step()` 在注册表里登记为 `gui_init`、`gui_step`。
- 播放与音频读取：`APP/tasks/playback/`，音乐专用任务，留在 `APP/tasks/`。
- `catalog/`：曲库（SD `Music/` 的路径事实表、播放列表、曲目显示名与标签文本、曲目打开、读取诊断），见 [catalog/README.md](catalog/README.md)。一份表、模块内一把短锁，发布时比对卷序号，读者经 `catalog_read_track()` 一次读全；扫描这时仍由 Storage Task 同步调用（#212，改成后台单步作业留给后续票）。
- 窗口槽、名称槽：`APP/tasks/storage/catalog/storage_listbuffer.*`、`storage_namebuffer.*`，不搬，前台一次切时删除（0.10.0 逐步搬走，见 [#202](https://github.com/immorcoding/mp3_project/issues/202) K 节）。
- LVGL 一半：`Service/gui/main/` 与 `view/`。

行为、旧三态与 Storage 窗口槽/名称槽的用法在这张票里完全没变；后续票把前台流程、界面数据和后台作业逐步搬进本目录。
