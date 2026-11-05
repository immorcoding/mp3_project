# Storage · catalog

曲目集合、顺序位置及跨任务可见快照。

[返回 Storage](storage.md)

### Rules

- **STOR-1** · settled · 曲库只收录 SD `Music/` 相对路径；播放列表、游标和窗口使用同一曲库代次，0 表示无效，GUI 只消费窗口副本；代次在发布时确定，扫描期间为 0，读者一次读全；重扫或拔卡作废旧代次。_Why:_ 下标仅在其快照内有效，避免跨任务共享整表和误用旧位置；扫描移到后台任务后，读者不能看到写了一半的表。_Source:_ [ADR-0015](../adr/0015-volume-roles-and-resource-install.md)、曲库接口（`APP/apps/music/catalog/`，随实施票建立）、[后台任务的作业模型](https://github.com/immorcoding/mp3_project/issues/196) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及 catalog 回归。
- **STOR-6** · settled · 窗口以界面数据交给 GUI，整份替换；需求只含播放列表起点，新的覆盖旧的；结果带实际代次、起点和实际条数；卷卸载或代次失效时发布代次 0 的空窗；窗口不携带当前曲，游标独立保存且只由播放任务写，GUI 不读写游标，当前曲以播放快照的曲目身份为准（GUI-17）。_Why:_ 可见窗口不是当前播放位置；整份替换让 GUI 只见完整的一窗。_Source:_ [界面数据与需求的交接形状](https://github.com/immorcoding/mp3_project/issues/193)、[ADR-0020](../adr/0020-playback-state-machine-ownership.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及窗口/游标回归。
- **STOR-7** · settled · 曲库根目录打不开按成功空库处理且不创建目录，槽用尽（过约 10 ms 重试）和卡已不在（丢弃本次扫描）不算；条目或池耗尽保留截断结果，深度耗尽跳过并告警，读/关目录错误返回失败；NOLOAD 曲库和播放表以有效表头及代次判定，不依赖整池清零。_Why:_ 明确空库、容量受限与 I/O 失败的不同产品结果；槽用尽和拔卡都不是空库。_Source:_ 曲库接口与扫描实现（`APP/apps/music/catalog/`，随实施票建立）、[后台任务的作业模型](https://github.com/immorcoding/mp3_project/issues/196) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及扫描边界回归。
- **STOR-15** · exploring · 曲目显示名有独立的需求和结果，与窗口互不争用；需求含代次和位置（代次 0 表示当前），结果带回所答的需求、实际代次、曲目显示名（有可用标题取标题，否则取文件主名，可直接显示的 UTF-8）和歌手（可为空串）；标题与歌手超长按 UTF-8 字符边界截断、不加省略号，文件主名不截断；读标签失败不是需求失败，照常发布并回退；代次不符、越界、卷未挂载或文件主名提取失败回代次 0 空名，只有代次相符仍失败的防御分支记 WARN；只由 GUI 音乐分区发需求，GUI 不解析路径。_Why:_ 名字不进播放快照，坏文件跳过时才能立即取候选名，也不与 Queue 换窗争用；标签缺失或损坏是常态，不能让曲名区退成 `--`。_Source:_ [曲目显示名的数据路径与所有权](https://github.com/immorcoding/mp3_project/issues/104)、[0.7.0 spec](https://github.com/immorcoding/mp3_project/issues/112)、[读标签的接口与槽](https://github.com/immorcoding/mp3_project/issues/133)、[0.8.0 spec](https://github.com/immorcoding/mp3_project/issues/135)、[界面数据与需求的交接形状](https://github.com/immorcoding/mp3_project/issues/193)
- **STOR-16** · exploring · 曲目标签只在前台任务处理需求时按需读 TIT2/TPE1，曲库扫描与建表不读；单个文件先读首块 4 KiB，按帧头 seek 跳读、不读 APIC 等负载，含首块最多 4 次读取且不越过标签总长（初值为配置常量，由板测修正）；每读完一行重新看一次最新需求，GUI 只见整窗；行缓存只保存结果稳定的行，打开失败、读取错误和 `SERVICE_BUSY` 不缓存，代次不符视为未命中；卷消抖中调用在文件接口入口等闸，填写随之暂停（STOR-8），恢复后续填，卷卸载才以代次 0 的空窗收尾；窗口行只含曲目显示名与歌手，不含路径。_Why:_ 扫描时读标签会拖长插卡后不可播放的时间；单行读取上界要小，前台任务才能及时看到新需求；瞬时错误不能让这首歌在换卡前一直显示文件主名。_Source:_ [读标签的时机与范围](https://github.com/immorcoding/mp3_project/issues/132)、[0.8.0 spec](https://github.com/immorcoding/mp3_project/issues/135)、[界面数据与需求的交接形状](https://github.com/immorcoding/mp3_project/issues/193)
- **STOR-17** · exploring · 曲库上限 12000 首，保证可同在一个文件夹（平均每首 ≤ 约 5.4 个 FAT 目录项）；曲库每条随附文件定位符（ARC-19），曲目显示名、Queue 窗口行、开启音频流与跳转重开流都按定位符打开，核对不符退回按路径打开并记 WARN（DIAG-5），其他失败不退回；单次打开 ≤ 约 30 ms、音乐队列一窗 12 行 ≤ 约 0.5 s，与文件在文件夹内的位置无关（初值，由板测修正）；1 s 是单行读取的日志阈值。_Why:_ 打开耗时直接是唱盘页曲名、Queue 滚动、开流与跳转的用户可感延迟；常见 16 / 32 GB FAT32 卡装不满 12000 首，更大的卡出厂为 exFAT、固件不挂载。_Source:_ [打开文件提速](https://github.com/immorcoding/mp3_project/issues/167)、[0.9.0 spec](https://github.com/immorcoding/mp3_project/issues/172)、[音频读取任务的迁移](https://github.com/immorcoding/mp3_project/issues/194)

### Rejected

- 名称槽只回文件主名、主名不截断即全部应答：0.8.0 起应答优先取曲目标签中的标题并带歌手，由 STOR-15 替代。（was STOR-14）
