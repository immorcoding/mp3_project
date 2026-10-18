# Playback Service 主机测试

本目录通过 `playback_service.h` 的公开接口验证播放模块（[#71](https://github.com/immorcoding/mp3_project/issues/71) D1–D12，缝 2）：编译真实的 `Service/playback/*.c` 与真实 minimp3（`Middlewares/Third_Party/minimp3/`），喂入 `fixtures/` 里的小 MP3 夹具。解码半边只观察 `Service_Playback_Advance()` 的回报与 `ReadFrame()` 取出的 PCM；输出管线链接 `fake_platform_audio.c`（假 `Platform_Audio_*`，不链接真实 Platform、FreeRTOS 或 HAL），观察假 Platform 记录的 Start/SubmitHalf/Stop 序列、环形缓冲内容与“DMA 读到的”播放日志。

五个 CTest：

| CTest | 覆盖 |
|---|---|
| `playback_service_stream` | 跨块喂入不丢帧（四个夹具 × 五种分块，PCM 哈希逐字节一致且非静音）；W_min 门槛；输入结束后不足 W_min 的余量原样解完；窗口接收量与结束后拒绝交入；新流丢弃旧窗口；流尾截断残余静默丢弃 |
| `playback_service_sync` | R 在垃圾字节上触发且保持；少于 R 的垃圾不触发；真实夹具带垃圾头不误判；流中垃圾后重同步；采样率不同的帧丢弃；异频帧计入 R |
| `playback_service_info` | 首帧采样率与声道数（只回报一次、落在带 PCM 的推进上）；总时长来自 Xing、VBRI 或按字节数估算；已解码时间逐帧累计、单调、钳在总时长；重开同一首从头计 |
| `playback_service_output` | 预填两个半区后 Start（采样率取自首帧、环形缓冲 32 B 对齐、预填不 SubmitHalf）；输入不够不 Start 且不丢音频；半区释放后补满下一段音频并按奇偶提交；三个 44.1 kHz 夹具按序放完、一帧不丢不重、流尾放完只 Stop 一次；静音半区先于最后数据半区的释放提交；一帧短文件；MPEG-2 夹具非整半区的流尾补 0；已解码时间不回退 |
| `playback_service_recovery` | 欠载 Stop → 补满 → Start 且接着解码进度；欠载恢复等输入；暂停 = Stop、暂停中不 Start/Submit/动半区、恢复 = Start；同曲恢复的半区重放与交换（奇数释放）；暂停时有未补的半区；多读的半区整体重来；暂停中换曲的预填；排空期间暂停；Start 被参数拒绝与被锁存拒绝；TRANSFER_ERROR 后不写半区；STOP_FAILED（暂停、欠载恢复、流尾、放弃时）之后不再写半区；暂停时 Stop 成功而读状态失败不算 Stop 失败、恢复后播完；放弃与 BeginStream 的 Stop；垃圾字节不可播；Advance 与 Pump 不混用；输出进展 |

运行（仓库根目录）：

```powershell
./scripts/test-host.ps1 -Module playback_service
```

## 夹具

`fixtures/` 的四个 MP3 与 `generate_fixtures.py` 的来源、规格和期望数字见 [fixtures/README.md](fixtures/README.md)。垃圾字节不放文件：测试用固定种子的线性同余发生器现场生成，结果与平台无关。

## 假 Platform 音频

`fake_platform_audio.c/.h` 实现 `platform_audio.h` 的全部公开函数，并按其合同建模：只收 32/44.1/48 kHz，ring 与半区字节数须按 32 B 对齐，运行中再 Start 被拒，Start 清零计数，Stop 成功后冻结计数，STOP_FAILED 锁存后 Start/Stop 一律拒绝，半区释放时另一半区未提交即计一次欠载。DMA 读到的是 Start/SubmitHalf 时 Clean 出去的副本而不是实时内存，所以写了半区却忘记提交会在播放日志里露出旧数据。测试专用的注入函数（`fake_audio_release_half`、`fake_audio_inject_transfer_error`、`fake_audio_fail_next_stop`、`fake_audio_latch_stop_failed`、`fake_audio_reject_next_start`、`fake_audio_fail_next_get_status`）只在假对象里，不进生产头。`fake_audio_accept_rate_for_test()` 额外接受一个采样率，只用来让 MPEG-2 夹具（576 样本帧，PCM 总长不是整半区）播放以覆盖补 0 的流尾；真实 Platform 仍拒绝该采样率，生产合同不变。

期望 PCM 来自解码半边的 `Advance`/`ReadFrame`（独立于输出路径）；CBR 夹具共 30 个整半区，测试断言播放日志与它逐字节相等。

## 不覆盖

解码耗时、栈水位、Start 阻塞预算、欠载听感、咔哒声和真实 DMA/Cache 行为只能上板，Host PASS 不是板级验收（`hw:pending`）。
