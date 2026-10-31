# Storage · media

物理分区、协议兼容与硬件访问所有权。

[返回 Storage](storage.md)

### Rules

- **STOR-2** · settled · Flash 诊断仅擦写首尾自检区，FTL 经 Bridge 校验完整分区范围后访问；固件双槽和原始资源区独立保留，格式化与 GC 同样受边界约束，首次破坏性访问核验已有内容和配置布局。_Why:_ 逻辑盘维护不能破坏镜像或资源。_Source:_ [ADR-0009](../adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md)、[介质事实](sources/storage-media.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 分区审阅及 Bridge/Platform 边界回归。
- **STOR-3** · settled · DMA 使用可达且独占完整 Cache line 的缓冲，方向相关维护和控制器停止确认完成后才归还；FatFs 任意缓冲经专用 bounce buffer 转交。FTL 整笔请求关闭 QSPI 映射，仅成功且硬件空闲后按入口状态恢复，失败保持关闭。_Why:_ IRQ 或软件超时不证明数据和映射已安全，不能让 Cache 维护影响相邻对象。_Source:_ [ADR-0006](../adr/0006-cache-range-ownership.md)、[介质事实](sources/storage-media.md) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的规范轴核查 Cache/生命周期，覆盖超时和延迟通知。
- **STOR-10** · settled · 原始 NOR 访问保持已选固定四字节协议、页/块边界和 QE/WEL 前提；恢复和开启映射先确认 NOR 空闲，SD 写传输完成还须等卡回 TRANSFER；改 CubeMX 存储配置同步核对 Adapter 初始化、回调注册、NVIC、QSPI/MDMA 配合及 CS 时序。_Why:_ 控制器忙与介质内部忙不同，吞吐通过不能证明时序或写入安全。_Source:_ [介质事实](sources/storage-media.md)、[SD Adapter](../../Adapters/stm32_hal/sd/sd_stm32_hal_adapter.c) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 的规范轴核查协议/时序，运行设备状态机回归与相关板测。

### Signals

- 2026-10-08 · cite · STOR-3 · 音频路径经 bounce buffer，不承诺零拷贝（bounce → 音频块 → 线性窗口）；音频块不作 DMA 目标，若改为 SDMMC 直写块，须另行决定并改写本规则。
