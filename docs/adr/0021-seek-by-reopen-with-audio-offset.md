# ADR-0021：跳转用带音频区间偏移的重开流，映射归 Service/playback

- 状态：已接受。
- 日期：2026-10-31
- 相关 ticket：[seek 的合同与流机制](https://github.com/immorcoding/mp3_project/issues/166)、[MP3 时间到文件偏移的映射](https://github.com/immorcoding/mp3_project/issues/161)、[0.9.0 spec](https://github.com/immorcoding/mp3_project/issues/172)
- 相关决定：[ADR-0017](0017-mp3-decoder-minimp3.md)、[ADR-0020](0020-playback-state-machine-ownership.md)；规则 ARC-20

## 背景

0.9.0 要做真跳转：用户在 Now Playing 拖动进度条，松手后从目标时间继续播放。现有音频流只能整条关闭再打开，流请求信箱只有 `{StreamSeq, Generation, Position}`，不带字节偏移；就绪队列里可能还有旧位置已读出的块。毫秒到字节的映射需要 MP3 知识（Xing/Info 的 TOC、帧长），Service/playback 已经解析这些头，Storage 只知道音频区间与文件长度。

## 决定

1. **跳转 = 带偏移重开一条新流。** 播放任务按切歌的清理路径处理跳转：新 StreamSeq、AbandonStream、丢弃过期块；Storage 关闭旧文件、重新打开，算出音频区间后 `f_lseek` 到区间起点 + 偏移。不在已打开的 FIL 内 seek。
2. **流请求信箱加 `AudioOffset`**（相对音频区间起点的字节数，普通开流为 0）。Storage 只把它钳到区间内，不认识音频格式；应答记录不变。
3. **毫秒 ↔ 字节映射归 Service/playback。** 有 TOC 按 TOC，否则（含 VBRI）按 `AudioBytes` 线性；越尾钳到 `AudioBytes − W_min`；落点 = 钳位后偏移按同一映射反算。Service 提供“从落点重开”：只清空解码器、输入窗口与计数，保留总时长、采样率、TOC 等流级事实。
4. **落点由播放任务在处理跳转的同一步写进快照**，再推进命令回显序号；之后不接受旧流的位置回报。
5. **不预滚、不淡入淡出。** 重同步与曲首相同，输出端与切歌一致（先停 I2S 再起播）。

## 未采用的方案

- **在原 FIL 内 `f_lseek` 并作废在途块。** 要为“同一条流内的位置跳变”另设一套块作废与身份规则；切歌的清理路径已经覆盖放弃旧块、重启计时，复用它更少新概念。重开的额外代价是一次打开，由按位置打开（[ADR-0022](0022-fatfs-open-by-locator-patch.md)）压到约 30 ms 以内。
- **映射放在 Storage。** Storage 要认识 MP3 头与 TOC，格式知识分散到两层；将来加别的格式时两处都要改。
- **映射放在播放任务。** 播放任务不解析帧头，TOC 只在 Service 解码路径里出现；搬上去要把解析结果整份交给 APP。
- **用千分比而不是毫秒作目标。** 总时长为估算值时千分比与毫秒等价，但毫秒让快照、日志与验收用同一单位。

## 后果

- 播放合同新增 `PLAYBACK_COMMAND_SEEK`、`TargetMs` 与快照 `TrackStartCount`（唱盘据此判断归零，因为跳转后位置可以变小）。
- Storage 不需要任何格式相关代码；新格式只改 Service/playback 的映射。
- 跳转时延 = 一次重开流（打开 + lseek + 首块 + 预填 + 起播），初值 ≤ 100 ms，由板测修正。
- 若将来要做无缝跳转（不停 I2S）或预滚，需要新 ADR 改写第 1、5 条。
