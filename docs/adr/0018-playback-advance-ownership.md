# ADR-0018：播放推进归 APP 后台任务，播放模块不持游标

- 状态：已接受。
- 日期：2026-10-08
- 相关 ticket：[0.6.0 地图](https://github.com/immorcoding/mp3_project/issues/29)、[Playback 状态机与接口](https://github.com/immorcoding/mp3_project/issues/35)
- 相关决定：[ADR-0002](0002-layering-and-interface-ownership.md)、[ADR-0015](0015-volume-roles-and-resource-install.md)、[ADR-0017](0017-mp3-decoder-minimp3.md)
- 相关技术文档：[Storage](../../APP/tasks/storage/README.md)、[Storage 规则](../shape/storage.md)

## 背景

0.6.0 要让 SD 上的 MP3 真正出声，并让播放、暂停、上一首、下一首接上真实状态。曲库、播放列表与播放游标已经在 APP/tasks/storage 中实现，游标自带环形上一首与下一首；GUI 音乐分区也留有"通知后台播放进程"的占位。

播放模块位于 Service 层。按 [ADR-0002](0002-layering-and-interface-ownership.md) 与 ARC-7，Service 不包含 APP 头，APP 负责编排。因此需要先决定三件事：谁推进曲目，播放模块能拿到什么，GUI 如何与它交互。

## 决定

1. **推进归 APP。** APP 后台播放任务读取播放游标，取得当前曲目并交给播放模块；当前曲播完或解码失败时，由该任务步进游标，再把新曲目交给播放模块。
2. **播放模块不持游标。** 播放游标留在 APP/tasks/storage；播放模块不包含游标头或曲库头，也不持有播放列表。
3. **播放模块自行打开文件。** 播放模块按卷相对路径经 Service/filesystem 打开、读取音频帧流、关闭；文件令牌由播放模块持有。卷拔出后的失效令牌在播放模块内转为停止。
4. **GUI 只经队列与播放交互。** GUI 向播放任务的命令队列投递命令，不读文件、不解码、不直接调用播放模块；播放进度由播放任务推送事件到 GUI 队列。
5. **播放任务与命令队列常驻。** 由 APP 在启动时创建，无曲目或列表未就绪时空闲等待。

状态机、列表推进、坏文件处理与各项行为细节不在本 ADR 中重复，见 [#35](https://github.com/immorcoding/mp3_project/issues/35) 的解决记录；随代码落地时写入 Service 播放模块的技术文档。

## 未采用的方案

- **游标迁入播放模块。** 播放模块将持有曲库顺序，需要把 `storage_playback_cursor_*` 的归属从 APP/tasks/storage 移出，并修订 Storage 的 STOR 规则与 GUI 读游标的路径。
- **播放模块通过 APP 注入的回调取下一首。** 功能上可行，但推进逻辑分散到两处，回调方向与 ARC-1 的依赖方向相反，需要额外解释。
- **GUI 直接调用播放模块的公开函数并加锁。** 播放状态被两个任务同时访问，需要互斥与可重入约束，且与 GUI 只投递命令的占位注释相反。
- **APP 打开文件，把句柄交给播放模块。** APP 要持有文件令牌的生命周期，拔卡与切歌的清理散在两处。

## 后果

- APP 播放任务是播放流程的唯一编排者；游标变化、切歌与失败处理都经过它。
- 播放模块只依赖 Filesystem 与音频输出的公开接口，不依赖曲库或游标，边界清楚。
- 常驻的播放任务与命令队列占用 RAM，需计入 0.6.0 的解码内存预算（见地图 Not yet specified）。
- GUI 进度为事件驱动，不依赖 GUI 轮询频率。
- 若将来要改成游标在播放模块内，需要新 ADR 取代本决定，并同步修订 Storage 规则与 GUI 音乐分区。
