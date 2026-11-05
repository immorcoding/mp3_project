# Architecture · storage-seams

文件系统与持久化层的公开接缝。

[返回 Architecture](architecture.md)

### Rules

- **ARC-11** · provisional · FatFs USER 接缝由 FATFS Target 自维护契约/安全弱定义、生成区薄转发和 Service 强定义组成；Filesystem 是一个 Module，SD/Flash 私有分区，Service 持有唯一完成订阅与执行等待（持卷锁的任务等每卷一个信号量），APP 决定时机；FTL 算法/GC/RawOps 属 Component，Platform 持有实例与映射生命周期。FatFs 不绕过 FTL 接原始 NOR；不预建 BlockDevice/Service/storage，USB MSC 不在产品范围。 _Why:_ 持久化算法、同步等待和任务策略需要各自唯一的拥有方。 _Source:_ [ADR-0010](../adr/0010-fatfs-user-diskio-service-ownership.md)、[ADR-0012](../adr/0012-filesystem-public-seams.md)、[ADR-0013](../adr/0013-usb-msc-product-scope.md)、[ADR-0024](../adr/0024-file-interface-concurrency.md)
- **ARC-12** · provisional · APP 文件访问只用 Filesystem 卷感知文件接口，不含 FatFs/逻辑块；Service 持有 FIL/DIR、路径/盘符转换和同步生命周期，APP 持有图样、计时、日志策略；GUI 和播放任务以外的任务可调用文件接口（入口断言，ARC-21），不建通用文件请求队列，有真实消费者的专用交接（如音频流请求与流记录信箱）不在此列，FIL/DIR 不离开 Service。删除释放 FAT 簇，不按文件地址擦 NOR；无 TRIM 时仅 LBA 后续更新才使旧物理版本失效，GC 归 FTL。 _Why:_ 应用不能绕开卷隔离和文件对象生命周期。 _Source:_ [ADR-0014](../adr/0014-filesystem-volume-aware-file-interface.md)、[ADR-0020](../adr/0020-playback-state-machine-ownership.md)、[ADR-0024](../adr/0024-file-interface-concurrency.md)
- **ARC-19** · exploring · Filesystem Service 扫描目录时随条目给出不透明的文件定位符，并按定位符打开文件；定位符内容、FIL 与 FatFs 类型都不出 Service（ARC-12）；按目录项位置打开只靠 `ff.c` 的一个补丁函数，原型只在 Service 私有头；定位符只在同一曲库代次内有效，打开时核对是文件、未删除、非目录、起始簇与长度一致，不符时返回可区分的结果，由调用方退回按路径打开。 _Why:_ FatFs 按路径打开每次从目录第 0 项逐项比较长文件名，耗时随位置线性增长，12000 首同文件夹时末尾超出预算，目录扇区缓存也压不下来；在 FatFs 外手拼 FIL 会让 `f_close` 的锁释放失败。 _Source:_ [ADR-0022](../adr/0022-fatfs-open-by-locator-patch.md)、[打开文件提速](https://github.com/immorcoding/mp3_project/issues/167)、[内存目录下 f_open 实测](https://github.com/immorcoding/mp3_project/issues/170)
- **ARC-20** · exploring · 跳转以带音频区间偏移的重开流实现，不在已打开的 FIL 内 seek：流请求带 `AudioOffset`（相对音频区间起点的字节数，普通开流为 0），音频读取任务只把它钳到区间内、不认识音频格式；毫秒与字节的互相映射（有 TOC 用 TOC，否则线性）与落点归 Service/playback，播放任务在处理跳转的同一步把落点写进快照。 _Why:_ 格式知识只在一处；复用切歌的清理路径（新流序号、放弃旧流、归还旧块），在途块与旧流身份不必另做作废。 _Source:_ [ADR-0021](../adr/0021-seek-by-reopen-with-audio-offset.md)、[seek 的合同与流机制](https://github.com/immorcoding/mp3_project/issues/166)
- **ARC-21** · provisional · Filesystem Service 一次公开调用只持有所属卷的一把卷锁，入口拿、返回前放；只有 `f_open`、`f_opendir` 和按定位符打开在卷锁内再拿表锁（卷锁 → 表锁，表锁内不拿别的锁）；`_FS_REENTRANT` 保持 0，`_FS_LOCK` 不小于槽总数；单次读写不超过 32 KiB；持文件锁时不调用会阻塞的接口；GUI 和播放任务进入口即断言停机。 _Why:_ SD 和 Flash 要互不影响：全局锁下 Flash 写入让切歌最慢到 1.5 s，宽表锁下删大文件让 SD 打开等约 2 s；一次只持一把就不会死锁。 _Source:_ [ADR-0024](../adr/0024-file-interface-concurrency.md)、[文件接口的锁边界](https://github.com/immorcoding/mp3_project/issues/192)

### Signals

- 2026-11-04 · cite · ARC-11 · 文字已改成没上板的新设计（每卷一个信号量、APP 决定时机），用户决定从 settled 降为 provisional；[0.10.0 spec](https://github.com/immorcoding/mp3_project/issues/202) 板级验收通过后升回 settled，并恢复 _Check:_（`CODING_STANDARDS.md` 的架构审阅入口）。
- 2026-11-04 · cite · ARC-12 · 同上（多任务调用文件接口、入口断言），验收通过后升回 settled，并恢复 _Check:_（`CODING_STANDARDS.md` 的架构审阅入口）。
- 2026-10-31 · friction · ARC-19 · 定位符里“文件夹起始簇 + 字节偏移”要求打开时沿目录簇链走到目标簇（`dir_sdi`），读扇区数随目标所在的目录簇序号增长（每个目录簇至多一次 FAT 扇区读），CPU 侧与位置无关但读扇区数不严格无关。32 KiB 簇、12000 首同文件夹的内存镜像实测：第 1 首 1 个扇区，最后一首 54 个扇区，按路径打开最后一首 3485 个扇区；真实卡上目录簇与文件数据簇交错，最坏约等于目录簇数次 FAT 扇区读，约 30 ms 的预算需板测确认；若超预算，可把目录项所在簇（或扇区）也放进定位符（16 B 变 20 B，或以它替换长度核对）。
