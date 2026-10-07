# Storage

曲库快照、存储卷、物理介质与持久化结果的约束；分层和执行所有权沿用 [Architecture](architecture.md) ARC-11/12。

Next id: STOR-14

## Pillars

- 曲库是路径事实，播放列表是顺序，窗口是局部副本。
- 完成同时满足持久化承诺和硬件所有权交还。
- 恢复保留可诊断的介质现状，不用旧数据掩盖损坏。

## Open questions

- MP3 元数据尚未实现：解析器、解码器与 PCM 发送保持独立，标签拟在填窗时按需读取，封面拟归 Now Playing；窗口载荷和缺标签文案待对应 spec 落定。

## catalog

曲目集合、顺序位置及跨任务可见快照。

### Rules

- **STOR-1** · settled · 曲库只收录 SD `Music/` 相对路径；播放列表、游标和窗口使用同一曲库代次，0 表示无效，GUI 只消费窗口副本；重扫或拔卡作废旧代次。_Why:_ 下标仅在其快照内有效，避免跨任务共享整表和误用旧位置。_Source:_ [ADR-0015](../adr/0015-volume-roles-and-resource-install.md)、[Storage 接口](../../APP/tasks/storage/README.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及 catalog/listbuffer 回归。
- **STOR-6** · settled · 窗口按空闲、请求、就绪、消费释放单槽交接；请求仅含播放列表起点、条数和代次，回包以实际条数界定副本范围，代次不符返回无效空窗；游标独立保存，高亮同时核对游标与窗口代次。_Why:_ 可见窗口不是当前播放位置，消费前释放或复用会混淆两次交接。_Source:_ [Storage 接口](../../APP/tasks/storage/README.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及窗口/游标回归。
- **STOR-7** · settled · 曲库根目录打不开按成功空库处理且不创建目录；条目或池耗尽保留截断结果，深度耗尽跳过并告警，读/关目录错误返回失败；NOLOAD 曲库和播放表以有效表头及代次判定，不依赖整池清零。_Why:_ 明确空库、容量受限与 I/O 失败的不同产品结果。_Source:_ [Storage 接口](../../APP/tasks/storage/README.md)、[扫描实现](../../APP/tasks/storage/catalog/storage_catalog.c) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及扫描边界回归。

## volumes

可移除状态、卷就绪及文件对象失效。

### Rules

- **STOR-8** · settled · 无卡是可正常启动的持续状态；检测边沿重开任务消抖并暂停窗口请求，稳定后才改变介质状态，消抖不等于卸载；事件等待兼顾消抖和回收期限，传输完成槽与主循环事件槽分开，挂载后继续使用初始化注入的完成槽。_Why:_ 抖动不能伪装传输结果或饿死后台回收，暂不可请求也不等于曲库已消失。_Source:_ [Storage 接口](../../APP/tasks/storage/README.md)、[SD 生命周期](../../Components/sd/README.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及热插拔/通知回归。
- **STOR-9** · settled · SD 与 Flash 独立初始化、挂载和报告错误；SD 无设备侧格式化，Flash 显式格式化或恢复成功时须已重新挂载并使本卷旧 Token 失效；APP 自动格式化策略只处理无文件系统结果，损坏、不兼容等其他失败保留可诊断状态。_Why:_ 一个卷的故障不能污染另一个卷或令旧对象透明继续写。_Source:_ [Filesystem 接口](../../Service/filesystem/README.md)、[APP 策略](../../APP/tasks/storage/flash/storage_flash.c) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及独立卷/Token/恢复回归。

## media

物理分区、协议兼容与硬件访问所有权。

### Rules

- **STOR-2** · settled · Flash 诊断仅擦写首尾自检区，FTL 经 Bridge 校验完整分区范围后访问；固件双槽和原始资源区独立保留，格式化与 GC 同样受边界约束，首次破坏性访问核验已有内容和配置布局。_Why:_ 逻辑盘维护不能破坏镜像或资源。_Source:_ [ADR-0009](../adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md)、[介质事实](sources/storage-media.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 分区审阅及 Bridge/Platform 边界回归。
- **STOR-3** · settled · DMA 使用可达且独占完整 Cache line 的缓冲，方向相关维护和控制器停止确认完成后才归还；FatFs 任意缓冲经专用 bounce buffer 转交。FTL 整笔请求关闭 QSPI 映射，仅成功且硬件空闲后按入口状态恢复，失败保持关闭。_Why:_ IRQ 或软件超时不证明数据和映射已安全，不能让 Cache 维护影响相邻对象。_Source:_ [ADR-0006](../adr/0006-cache-range-ownership.md)、[介质事实](sources/storage-media.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的规范轴核查 Cache/生命周期，覆盖超时和延迟通知。
- **STOR-10** · settled · 原始 NOR 访问保持已选固定四字节协议、页/块边界和 QE/WEL 前提；恢复和开启映射先确认 NOR 空闲，SD 写传输完成还须等卡回 TRANSFER；改 CubeMX 存储配置同步核对 Adapter 初始化、回调注册、NVIC、QSPI/MDMA 配合及 CS 时序。_Why:_ 控制器忙与介质内部忙不同，吞吐通过不能证明时序或写入安全。_Source:_ [介质事实](sources/storage-media.md)、[SD Adapter](../../Adapters/stm32_hal/sd/sd_stm32_hal_adapter.c) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的规范轴核查协议/时序，运行设备状态机回归与相关板测。

## durability

FTL 持久化格式、可见提交和故障恢复。

### Rules

- **STOR-4** · settled · FTL 在异地数据与末尾提交页均验证后切换映射并确认单组完成；读取和恢复的最新已提交版本损坏报错，不静默回退；跨组请求及 FatFs 文件事务不承诺整体原子。_Why:_ 已确认数据不能被旧版本冒充，单组提交不能扩大为文件事务保证。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 持久化审阅、Fake NOR 撕裂写/CRC/跨组回归及真实掉电验收。
- **STOR-5** · settled · 恢复选最新可验证 epoch，最新 PREPARING 不被旧 READY 掩盖；双卷头按准备完成后才破坏数据、就绪完成后才确认的顺序更新；FTL/Service 挂载失败不自行格式化，显式恢复先安全收尾再扫描并作废旧文件对象。_Why:_ 恢复不能覆盖现状或重解释未完成格式化。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md)、[介质提交依据](sources/storage-format.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 持久化审阅及双卷头各阶段掉电/未格式化/恢复生命周期回归。
- **STOR-11** · settled · 格式逐字段显式编码并校验几何兼容；每次打开重建 RAM 映射，先按有效提交选择版本再验证获选整块；新块全块擦除须确认，单页每擦除周期只编程一次，跳过全擦除值页仍读回校验，局部更新保留同组其他扇区。_Why:_ 旧失效块残片、NOLOAD 和空元数据页都不能充当有效性证明。_Source:_ [FTL 格式](sources/storage-format.md)、[ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 持久化审阅及恢复分类/局部更新/跳页/兼容性回归。

## execution

回收推进、故障停机及完成判定。

### Rules

- **STOR-12** · settled · GC 只擦已证明不承载当前数据的块；已擦除空闲数按全池预留预算和滞回水位管理，分配保持保护下限，无可回收块即结束；上层提供有限回收机会，当前一次最多一块，不承诺擦除抢占或静态磨损均衡。_Why:_ 回收是有限进展而非必然释放空间，压力不能成为擦当前版本的理由。_Source:_ [ADR-0011](../adr/0011-ftl-copy-on-write-and-recovery.md)、[FTL 接口](../../Components/flash_ftl/README.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 回收审阅及水位/无失效块/持续覆盖回归。
- **STOR-13** · settled · 执行器区分软件可继续与等待硬件，FTL 内部原始操作只由 FTL 推进；错误后停接新请求，校验失败保留旧映射不自动换块重试，Quiesce 未确认前保持缓冲所有权。Sync 确认先前写入与硬件安全而非清空 GC；失败返回不能解释为介质未变化。_Why:_ 软件步骤未必有 IRQ，Abort 不撤销 NOR 写擦，提交后通知也可能丢失。_Source:_ [FTL 接口](../../Components/flash_ftl/README.md)、[故障模型](sources/storage-format.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的规范轴核查推进/安全收尾，覆盖通知丢失、中止失败与介质仍忙。
