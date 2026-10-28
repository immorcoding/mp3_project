# Storage

曲库快照、存储卷、物理介质与持久化结果的约束；分层和执行所有权沿用 [Architecture](architecture.md) ARC-11/12。

Next id: STOR-17

## Pillars

- 曲库是路径事实，播放列表是顺序，窗口是局部副本。
- 完成同时满足持久化承诺和硬件所有权交还。
- 恢复保留可诊断的介质现状，不用旧数据掩盖损坏。

## Open questions

- MP3 封面的展示：拟归 Now Playing，载荷待对应 spec 落定。播放时越过 ID3v2 与尾部 ID3v1/APE 归 Storage（见词典“音频帧流”），不属本问题。
- SD 读取超过 `FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS`（500 ms，按每个 32 KiB DMA 分块）后，迟到的 SDMMC DMA 完成事件是否会被下一次传输误当作完成：超时只把设备置 ERROR 并不中止在途 DMA，下次传输前会清通知挂起位，残余窗口是迟到 IRQ 落在恢复之后、清位之后，而通知值不带传输序号。主机测试无法证实，待上板构造超时（拔卡或低速卡）后紧接读写；成立则给通知加传输序号，或在恢复路径显式 Abort DMA（hw:pending，见板上状态）。

## catalog

曲目集合、顺序位置及跨任务可见快照。

### Rules

- **STOR-1** · settled · 曲库只收录 SD `Music/` 相对路径；播放列表、游标和窗口使用同一曲库代次，0 表示无效，GUI 只消费窗口副本；重扫或拔卡作废旧代次。_Why:_ 下标仅在其快照内有效，避免跨任务共享整表和误用旧位置。_Source:_ [ADR-0015](../adr/0015-volume-roles-and-resource-install.md)、[Storage 接口](../../APP/tasks/storage/README.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及 catalog/listbuffer 回归。
- **STOR-6** · settled · 窗口按空闲、请求、就绪、消费释放单槽交接；请求仅含播放列表起点、条数和代次，回包以实际条数界定副本范围，代次不符返回无效空窗；窗口不携带当前曲，游标独立保存且只由播放任务写，GUI 不读写游标，当前曲以播放快照的曲目身份为准（GUI-17）。_Why:_ 可见窗口不是当前播放位置，消费前释放或复用会混淆两次交接。_Source:_ [Storage 接口](../../APP/tasks/storage/README.md)、[ADR-0020](../adr/0020-playback-state-machine-ownership.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及窗口/游标回归。
- **STOR-7** · settled · 曲库根目录打不开按成功空库处理且不创建目录；条目或池耗尽保留截断结果，深度耗尽跳过并告警，读/关目录错误返回失败；NOLOAD 曲库和播放表以有效表头及代次判定，不依赖整池清零。_Why:_ 明确空库、容量受限与 I/O 失败的不同产品结果。_Source:_ [Storage 接口](../../APP/tasks/storage/README.md)、[扫描实现](../../APP/tasks/storage/catalog/storage_catalog.c) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及扫描边界回归。
- **STOR-15** · exploring · 曲目显示名经独立于窗口的名称单槽交接，按空闲、请求、就绪、消费释放；请求只含代次与播放列表位置（代次 0 表示当前），应答带实际代次、曲目显示名（有可用标题取标题，否则取文件主名，可直接显示的 UTF-8）和歌手（可为空串）；标题与歌手超长按 UTF-8 字符边界截断、不加省略号，文件主名不截断；读标签失败不是槽失败，照常就绪并回退；代次不符、越界、SD 未就绪或文件主名提取失败回代次 0 空名，只有代次相符仍失败的防御分支记 WARN；只由 GUI 音乐分区请求，GUI 不解析路径。_Why:_ 名字不进播放快照，坏文件跳过时才能立即取候选名，也不与 Queue 换窗争用同一槽；标签缺失或损坏是常态，不能让曲名区退成 `--`。_Source:_ [曲目显示名的数据路径与所有权](https://github.com/immorcoding/mp3_project/issues/104)、[0.7.0 spec](https://github.com/immorcoding/mp3_project/issues/112)、[读标签的接口与槽](https://github.com/immorcoding/mp3_project/issues/133)、[0.8.0 spec](https://github.com/immorcoding/mp3_project/issues/135)
- **STOR-16** · exploring · 曲目标签只在名称槽与 Queue 窗口槽 `load` 时按需读 TIT2/TPE1，曲库扫描与建表不读；单个文件先读首块 4 KiB，按帧头 seek 跳读、不读 APIC 等负载，含首块最多 4 次读取且不越过标签总长（初值为配置常量，由板测修正）；窗口槽每圈最多读一行，读完前保持 `PENDING`，GUI 只见整窗；0.8.0 不缓存，每次 `load` 重读；窗口行只含曲目显示名与歌手，不含路径。_Why:_ 扫描时读标签会拖长插卡后不可播放的时间；单行读取上界须小于 Storage 单圈余量 T_data/2，逐圈分摊才不饿死音频流。_Source:_ [读标签的时机与范围](https://github.com/immorcoding/mp3_project/issues/132)、[0.8.0 spec](https://github.com/immorcoding/mp3_project/issues/135)

### Signals

- 2026-10-27 · friction · STOR-16 · #135 H 节写“途中代次变化或 SD 离开就绪以无效空窗收尾”，而 Storage Task 把消抖中也算“离开就绪”；STOR-8（settled）要求消抖只暂缓窗口请求、不等于卸载。#140 按 STOR-8 实现：消抖中窗口暂停逐行填写，恢复就绪后按 Sheet 代次续填或以空窗收尾，只有消抖后未挂载才经 `complete_unavailable()` 收尾。建议 STOR-16 把“SD 离开就绪”写成“卷卸载”，并注明消抖中暂停。

### Rejected

- 名称槽只回文件主名、主名不截断即全部应答：0.8.0 起应答优先取曲目标签中的标题并带歌手，由 STOR-15 替代。（was STOR-14）

## volumes

可移除状态、卷就绪及文件对象失效。

### Rules

- **STOR-8** · settled · 无卡是可正常启动的持续状态；检测边沿重开任务消抖，并暂缓窗口请求、音频流请求与补块（已打开的流保持打开），稳定后才改变介质状态，消抖不等于卸载；事件等待兼顾消抖和回收期限，传输完成槽与主循环事件槽分开，挂载后继续使用初始化注入的完成槽。_Why:_ 抖动不能伪装传输结果或饿死后台回收，暂不可请求也不等于曲库已消失。_Source:_ [Storage 接口](../../APP/tasks/storage/README.md)、[SD 生命周期](../../Components/sd/README.md)、[音频流协议](https://github.com/immorcoding/mp3_project/issues/48) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及热插拔/通知回归。
- **STOR-9** · settled · SD 与 Flash 独立初始化、挂载和报告错误；SD 无设备侧格式化，Flash 显式格式化或恢复成功时须已重新挂载并使本卷旧 Token 失效；APP 自动格式化策略只处理无文件系统结果，损坏、不兼容等其他失败保留可诊断状态。_Why:_ 一个卷的故障不能污染另一个卷或令旧对象透明继续写。_Source:_ [Filesystem 接口](../../Service/filesystem/README.md)、[APP 策略](../../APP/tasks/storage/flash/storage_flash.c) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及独立卷/Token/恢复回归。

### Signals

- 2026-10-09 · friction · STOR-8 · 插卡消抖后同步挂载并扫描（最多 32000 首，可能数秒），期间不处理流请求，插卡后立即播放会触发 T_data 停止（已接受）；改为分步扫描须同时改写主循环结构与 T_data 的依据。

## media

物理分区、协议兼容与硬件访问所有权。

### Rules

- **STOR-2** · settled · Flash 诊断仅擦写首尾自检区，FTL 经 Bridge 校验完整分区范围后访问；固件双槽和原始资源区独立保留，格式化与 GC 同样受边界约束，首次破坏性访问核验已有内容和配置布局。_Why:_ 逻辑盘维护不能破坏镜像或资源。_Source:_ [ADR-0009](../adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md)、[介质事实](sources/storage-media.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 分区审阅及 Bridge/Platform 边界回归。
- **STOR-3** · settled · DMA 使用可达且独占完整 Cache line 的缓冲，方向相关维护和控制器停止确认完成后才归还；FatFs 任意缓冲经专用 bounce buffer 转交。FTL 整笔请求关闭 QSPI 映射，仅成功且硬件空闲后按入口状态恢复，失败保持关闭。_Why:_ IRQ 或软件超时不证明数据和映射已安全，不能让 Cache 维护影响相邻对象。_Source:_ [ADR-0006](../adr/0006-cache-range-ownership.md)、[介质事实](sources/storage-media.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的规范轴核查 Cache/生命周期，覆盖超时和延迟通知。
- **STOR-10** · settled · 原始 NOR 访问保持已选固定四字节协议、页/块边界和 QE/WEL 前提；恢复和开启映射先确认 NOR 空闲，SD 写传输完成还须等卡回 TRANSFER；改 CubeMX 存储配置同步核对 Adapter 初始化、回调注册、NVIC、QSPI/MDMA 配合及 CS 时序。_Why:_ 控制器忙与介质内部忙不同，吞吐通过不能证明时序或写入安全。_Source:_ [介质事实](sources/storage-media.md)、[SD Adapter](../../Adapters/stm32_hal/sd/sd_stm32_hal_adapter.c) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的规范轴核查协议/时序，运行设备状态机回归与相关板测。

### Signals

- 2026-10-08 · cite · STOR-3 · 音频路径经 bounce buffer，不承诺零拷贝（bounce → 音频块 → 线性窗口）；音频块不作 DMA 目标，若改为 SDMMC 直写块，须另行决定并改写本规则。

## durability

FTL 持久化格式、可见提交和故障恢复。

### Rules

- **STOR-4** · settled · FTL 在异地数据与末尾提交页均验证后切换映射并确认单组完成；读取和恢复的最新已提交版本损坏报错，不静默回退；跨组请求及 FatFs 文件事务不承诺整体原子。_Why:_ 已确认数据不能被旧版本冒充，单组提交不能扩大为文件事务保证。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 持久化审阅、Fake NOR 撕裂写/CRC/跨组回归及真实掉电验收。
- **STOR-5** · settled · 恢复选最新可验证 epoch，最新 PREPARING 不被旧 READY 掩盖；双卷头按准备完成后才破坏数据、就绪完成后才确认的顺序更新；FTL/Service 挂载失败不自行格式化，显式恢复先安全收尾再扫描并作废旧文件对象。_Why:_ 恢复不能覆盖现状或重解释未完成格式化。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md)、[介质提交依据](sources/storage-format.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 持久化审阅及双卷头各阶段掉电/未格式化/恢复生命周期回归。
- **STOR-11** · settled · 格式逐字段显式编码并校验几何兼容；每次打开重建 RAM 映射，先按有效提交选择版本再验证获选整块；新块全块擦除须确认，单页每擦除周期只编程一次，跳过全擦除值页仍读回校验，局部更新保留同组其他扇区。_Why:_ 旧失效块残片、NOLOAD 和空元数据页都不能充当有效性证明。_Source:_ [FTL 格式](sources/storage-format.md)、[ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 持久化审阅及恢复分类/局部更新/跳页/兼容性回归。

## execution

回收推进、故障停机及完成判定。

### Rules

- **STOR-12** · settled · GC 只擦已证明不承载当前数据的块；已擦除空闲数按全池预留预算和滞回水位管理，分配保持保护下限，无可回收块即结束；上层提供有限回收机会，有打开的音频流时不提供（暂停保留流，回收顺延），当前一次最多一块，不承诺擦除抢占或静态磨损均衡。_Why:_ 回收是有限进展而非必然释放空间，压力不能成为擦当前版本的理由。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md)、[FTL 接口](../../Components/flash_ftl/README.md)、[音频流协议](https://github.com/immorcoding/mp3_project/issues/48) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 回收审阅及水位/无失效块/持续覆盖回归。
- **STOR-13** · settled · 执行器区分软件可继续与等待硬件，FTL 内部原始操作只由 FTL 推进；错误后停接新请求，校验失败保留旧映射不自动换块重试，Quiesce 未确认前保持缓冲所有权。Sync 确认先前写入与硬件安全而非清空 GC；失败返回不能解释为介质未变化。_Why:_ 软件步骤未必有 IRQ，Abort 不撤销 NOR 写擦，提交后通知也可能丢失。_Source:_ [FTL 接口](../../Components/flash_ftl/README.md)、[故障模型](sources/storage-format.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的规范轴核查推进/安全收尾，覆盖通知丢失、中止失败与介质仍忙。
