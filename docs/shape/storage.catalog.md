# Storage · catalog

曲目集合、顺序位置及跨任务可见快照。

[返回 Storage](storage.md)

### Rules

- **STOR-1** · settled · 曲库只收录 SD `Music/` 相对路径；播放列表、游标和窗口使用同一曲库代次，0 表示无效，GUI 只消费窗口副本；重扫或拔卡作废旧代次。_Why:_ 下标仅在其快照内有效，避免跨任务共享整表和误用旧位置。_Source:_ [ADR-0015](../adr/0015-volume-roles-and-resource-install.md)、[Storage 接口](../../APP/tasks/storage/README.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及 catalog/listbuffer 回归。
- **STOR-6** · settled · 窗口按空闲、请求、就绪、消费释放单槽交接；请求仅含播放列表起点、条数和代次，回包以实际条数界定副本范围，代次不符返回无效空窗；窗口不携带当前曲，游标独立保存且只由播放任务写，GUI 不读写游标，当前曲以播放快照的曲目身份为准（GUI-17）。_Why:_ 可见窗口不是当前播放位置，消费前释放或复用会混淆两次交接。_Source:_ [Storage 接口](../../APP/tasks/storage/README.md)、[ADR-0020](../adr/0020-playback-state-machine-ownership.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及窗口/游标回归。
- **STOR-7** · settled · 曲库根目录打不开按成功空库处理且不创建目录；条目或池耗尽保留截断结果，深度耗尽跳过并告警，读/关目录错误返回失败；NOLOAD 曲库和播放表以有效表头及代次判定，不依赖整池清零。_Why:_ 明确空库、容量受限与 I/O 失败的不同产品结果。_Source:_ [Storage 接口](../../APP/tasks/storage/README.md)、[扫描实现](../../APP/tasks/storage/catalog/storage_catalog.c) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及扫描边界回归。
- **STOR-15** · exploring · 曲目显示名经独立于窗口的名称单槽交接，按空闲、请求、就绪、消费释放；请求只含代次与播放列表位置（代次 0 表示当前），应答带实际代次、曲目显示名（有可用标题取标题，否则取文件主名，可直接显示的 UTF-8）和歌手（可为空串）；标题与歌手超长按 UTF-8 字符边界截断、不加省略号，文件主名不截断；读标签失败不是槽失败，照常就绪并回退；代次不符、越界、SD 未就绪或文件主名提取失败回代次 0 空名，只有代次相符仍失败的防御分支记 WARN；只由 GUI 音乐分区请求，GUI 不解析路径。_Why:_ 名字不进播放快照，坏文件跳过时才能立即取候选名，也不与 Queue 换窗争用同一槽；标签缺失或损坏是常态，不能让曲名区退成 `--`。_Source:_ [曲目显示名的数据路径与所有权](https://github.com/immorcoding/mp3_project/issues/104)、[0.7.0 spec](https://github.com/immorcoding/mp3_project/issues/112)、[读标签的接口与槽](https://github.com/immorcoding/mp3_project/issues/133)、[0.8.0 spec](https://github.com/immorcoding/mp3_project/issues/135)
- **STOR-16** · exploring · 曲目标签只在名称槽与 Queue 窗口槽 `load` 时按需读 TIT2/TPE1，曲库扫描与建表不读；单个文件先读首块 4 KiB，按帧头 seek 跳读、不读 APIC 等负载，含首块最多 4 次读取且不越过标签总长（初值为配置常量，由板测修正）；窗口槽每圈最多读一行，读完前保持 `PENDING`，GUI 只见整窗；0.8.0 不缓存，每次 `load` 重读；卷消抖中暂停逐行填写（STOR-8），恢复就绪后续填，卷卸载才以无效空窗收尾；窗口行只含曲目显示名与歌手，不含路径。_Why:_ 扫描时读标签会拖长插卡后不可播放的时间；单行读取上界须小于 Storage 单圈余量 T_data/2，逐圈分摊才不饿死音频流。_Source:_ [读标签的时机与范围](https://github.com/immorcoding/mp3_project/issues/132)、[0.8.0 spec](https://github.com/immorcoding/mp3_project/issues/135)
- **STOR-17** · exploring · 曲库上限 12000 首，保证可同在一个文件夹（平均每首 ≤ 约 5.4 个 FAT 目录项）；曲库每条随附文件定位符（ARC-19），名称槽、Queue 窗口行、开启音频流与跳转重开流都按定位符打开，核对不符退回按路径打开并记 WARN（DIAG-5）；单次打开 ≤ 约 30 ms、音乐队列一窗 12 行 ≤ 约 0.5 s，与文件在文件夹内的位置无关（初值，由板测修正）；T_data/2 仍是单行读取的防御阈值。_Why:_ 打开耗时直接是唱盘页曲名、Queue 滚动、开流与跳转的用户可感延迟；常见 16 / 32 GB FAT32 卡装不满 12000 首，更大的卡出厂为 exFAT、固件不挂载。_Source:_ [打开文件提速](https://github.com/immorcoding/mp3_project/issues/167)、[0.9.0 spec](https://github.com/immorcoding/mp3_project/issues/172)

### Rejected

- 名称槽只回文件主名、主名不截断即全部应答：0.8.0 起应答优先取曲目标签中的标题并带歌手，由 STOR-15 替代。（was STOR-14）
