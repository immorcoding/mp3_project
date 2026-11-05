# Diagnostics

错误解释与日志交付边界；通用错误模型沿用 [Architecture](architecture.md) ARC-9，ISR 边界沿用 ARC-10；调试中的易误判情况见 [错误观察对照](sources/diagnostic-observations.md)。

Next id: DIAG-8

## Pillars

- 一次调用结果不替代持续生命周期。
- 日志入队、被输出接受和主机显示分别判断。
- 诊断保留故障证据，不改变产品执行与缓冲所有权。

## logging

日志的并发接缝与异步缓冲寿命。

### Rules

- **DIAG-1** · settled · 调度器启动后只有 Log Task 消费无锁日志核心；其他普通任务经先初始化的 Service_Log 非阻塞投递，tag 使用静态存储期字符串，文本由消息块复制。_Why:_ 核心不支持并发，队列复制块指针而不延长 tag 生命周期。_Source:_ [ADR-0004](../adr/0004-log-single-consumer-and-output-adapter.md)、[Service Log](../../Service/log/README.md) _Check:_ code-review 规范轴 核对生产者、单消费者和消息所有权。
- **DIAG-2** · settled · 异步输出 Adapter 持有独立持久发送缓冲，提交后直到后端结束读取才复用；核心出队只表示 Adapter 已接受，不表示主机已收到。_Why:_ 出队后的核心槽位可立即复用，USB 仍异步读取提交指针。_Source:_ [USB 日志 Adapter](../../Adapters/stm32_hal/log_usb_cdc/README.md) _Check:_ code-review 规范轴核对异步提交、完成与缓冲复用。
- **DIAG-3** · provisional · ISR 回调与每帧路径不调用 `Service_Log_Post`；播放 INFO 只在命令与曲目边界由播放任务记（标签 `PLAYBACK`），文本只含曲库序号、停止原因名与数值字段（时间、字节偏移、耗时），不含文件名；INFO 日志记已发生的结果，不记意图，且在动作成功后记。_Why:_ 日志投递占消息池且不属实时预算；文件名转换属曲名数据路径。_Source:_ [播放任务](../../APP/tasks/playback/README.md)、[#108](https://github.com/immorcoding/mp3_project/issues/108)、[0.9.0 板级验收](https://github.com/immorcoding/mp3_project/issues/168)
- **DIAG-4** · exploring · 读标签按六类原因计数：无 ID3v2、帧缺失、解码失败、超读取上限、打开失败、读取错误；无 ID3v2 与帧缺失是正常情形，只计数；曲名需求每次读取记一行 INFO（位置、来源与原因名、读取次数、标签读取耗时 µs，打开耗时另记 `open=` µs、与标签读取分开），后四类原因另记一行 WARN；Queue 窗口逐行不记，加载完成时仅在本窗出现 WARN 级原因、单行打开超过 30 ms 或整窗超过 0.5 s（初值，配置常量）时记一行汇总，1 s 保留为单行日志阈值；文本只含播放列表位置与原因名，不含文件名、标题或歌手。_Why:_ 大量无标签曲目不能刷屏，滚动 Queue 不能占满消息池；汇总阈值跟随用户感受到的打开预算（STOR-17）；文件名与标题的转换属曲名数据路径（同 DIAG-3）。_Source:_ [标签诊断与验收](https://github.com/immorcoding/mp3_project/issues/134)、[0.8.0 spec](https://github.com/immorcoding/mp3_project/issues/135)、[0.9.0 板级验收](https://github.com/immorcoding/mp3_project/issues/168)
- **DIAG-5** · exploring · 跳转与开流只在边界上记日志：播放任务在每次真正执行的跳转成功后记一行 `Seek` INFO（PLAYING 在输出重新起播时，PAUSED 在预填完成时；含序号、目标、落点、字节偏移、映射方式与是否钳位、DWT 时延），被忽略的只记 DEBUG，被合并或作废的不记；音频读取任务每次开流与重开流记一行 `Stream` INFO（位置、偏移、打开与 lseek 耗时）；文件定位符核对失败记 `Locator miss` WARN 并累加计数；文本只含序号、数值与原因名，不含文件名。_Why:_ 跳转精度、时延与打开耗时要有固件出口才能板测（WF-12）；连续拖动不能刷屏占满消息池。_Source:_ [0.9.0 板级验收](https://github.com/immorcoding/mp3_project/issues/168)、[0.9.0 spec](https://github.com/immorcoding/mp3_project/issues/172)

### References

- [Service Log](../../Service/log/README.md)：消息池所有权与任务接缝；[Log Component](../../Components/log/README.md)：核心队列；[USB Adapter](../../Adapters/stm32_hal/log_usb_cdc/README.md)：就绪和异步输出。

### Signals

- 2026-11-01 · friction · DIAG-4 · 曲名 INFO 的 `us=` 现为标签读取耗时（整行耗时减去 `open=`），窗口汇总行的 `max=`、`us=`、`top=` 仍含打开；汇总行新增 `omax=<本窗最长单次打开>`，其中一个触发条件仍是单行读取（含打开）超过防御阈值（0.10.0 前是 T_data/2，之后是 1 s）。建议 DIAG-4 的文字写明“`us=` 不含打开、`max/us/top` 含打开、新增 `omax=`”。汇总行 12 行窗口、十位数极端取值会超出 Service Log 单条 128 B 并丢尾部计数（现实取值约 120 字符）。
- 2026-11-01 · friction · DIAG-5 · Storage `Stream` INFO 只在开启成功后记（`off=` 为请求的 `AudioOffset`，未钳位；`lseek=` 是最后一次定位到区间起点加偏移的耗时）；开启失败不记，失败时只有停止原因可查。需要失败也留痕时另开票（#179）。
- 2026-11-03 · friction · DIAG-3 · 板测要的数字多（#191 约 20 行每 10 s），Service Log 消息池只有 30 条，Monitor 每 5 s 连发约 13 行，撞上就挤掉 PLAYBACK 等日志；spike 靠每行之后让出 6 ms 绕过，没有改池深度。汇总类日志要注明节拍，或随汇总需求复核池深度（同时引 WF-12）。

## evidence

板测证据的总原则：时延怎么算、哪些常开、测试钩子。

### Rules

- **DIAG-6** · exploring · 板测用的时延一律在固件里算好再输出，日志时间戳是输出时刻，不能相减；边界上的结果常开（切歌到出声、扫描结束、卷路径、Abort 结果、写 `CARD_REMOVED` 的时延）；周期汇总进 Monitor 5 s 一批（欠载、CPU 占比、日志丢失数）；持锁统计（每把锁的最长持有、最长等待和对应操作，含 `ui_cell` 和曲库短锁）和显示延迟汇总走同一个编译开关，Debug 开、Release 关，只出汇总。_Why:_ 验收项要有固件出口（WF-12）；消息池只有 30 条，逐次打点会挤掉别的日志。_Source:_ [0.10.0 板级验收](https://github.com/immorcoding/mp3_project/issues/198)、[迁移切片与分支顺序](https://github.com/immorcoding/mp3_project/issues/201)、[0.10.0 spec](https://github.com/immorcoding/mp3_project/issues/202)
- **DIAG-7** · exploring · 产品固件走不到的验收场景用测试钩子：合入主线，每个场景一个默认关闭的编译开关，写成 `if (开关)` 让默认构建也编译，发布构建不开；启动日志打印开了哪些钩子。_Why:_ 证据要留在最终提交上，不靠丢弃分支；默认也编译，钩子才不会烂掉，FULL 也不用多编一版。_Source:_ [0.10.0 板级验收](https://github.com/immorcoding/mp3_project/issues/198)、[迁移切片与分支顺序](https://github.com/immorcoding/mp3_project/issues/201)
