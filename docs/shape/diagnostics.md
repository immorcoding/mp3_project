# Diagnostics

错误解释与日志交付边界；通用错误模型沿用 [Architecture](architecture.md) ARC-9，ISR 边界沿用 ARC-10；调试中的易误判情况见 [错误观察对照](sources/diagnostic-observations.md)。

Next id: DIAG-6

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
- **DIAG-4** · exploring · Storage 读标签按六类原因计数：无 ID3v2、帧缺失、解码失败、超读取上限、打开失败、读取错误；无 ID3v2 与帧缺失是正常情形，只计数；名称槽每次 `load` 记一行 INFO（位置、来源与原因名、读取次数、标签读取耗时 µs，打开耗时另记 `open=` µs、与标签读取分开），后四类原因另记一行 WARN；Queue 窗口逐行不记，加载完成时仅在本窗出现 WARN 级原因、单行打开超过 30 ms 或整窗超过 0.5 s（初值，配置常量）时记一行汇总，T_data/2 保留为单行防御阈值；文本只含播放列表位置与原因名，不含文件名、标题或歌手。_Why:_ 大量无标签曲目不能刷屏，滚动 Queue 不能占满消息池；汇总阈值跟随用户感受到的打开预算（STOR-17）；文件名与标题的转换属曲名数据路径（同 DIAG-3）。_Source:_ [标签诊断与验收](https://github.com/immorcoding/mp3_project/issues/134)、[0.8.0 spec](https://github.com/immorcoding/mp3_project/issues/135)、[0.9.0 板级验收](https://github.com/immorcoding/mp3_project/issues/168)
- **DIAG-5** · exploring · 跳转与开流只在边界上记日志：播放任务在每次真正执行的跳转成功后记一行 `Seek` INFO（PLAYING 在输出重新起播时，PAUSED 在预填完成时；含序号、目标、落点、字节偏移、映射方式与是否钳位、DWT 时延），被忽略的只记 DEBUG，被合并或作废的不记；Storage 每次开流与重开流记一行 `Stream` INFO（位置、偏移、打开与 lseek 耗时）；文件定位符核对失败记 `Locator miss` WARN 并累加计数；文本只含序号、数值与原因名，不含文件名。_Why:_ 跳转精度、时延与打开耗时要有固件出口才能板测（WF-12）；连续拖动不能刷屏占满消息池。_Source:_ [0.9.0 板级验收](https://github.com/immorcoding/mp3_project/issues/168)、[0.9.0 spec](https://github.com/immorcoding/mp3_project/issues/172)

### References

- [Service Log](../../Service/log/README.md)：消息池所有权与任务接缝；[Log Component](../../Components/log/README.md)：核心队列；[USB Adapter](../../Adapters/stm32_hal/log_usb_cdc/README.md)：就绪和异步输出。
