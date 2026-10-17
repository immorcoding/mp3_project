# Playback Service 主机测试

本目录通过 `playback_service.h` 的公开接口验证播放模块的**解码半边**（[#71](https://github.com/immorcoding/mp3_project/issues/71) D1–D6、D11 的解码部分，缝 2）：编译真实的 `Service/playback/*.c` 与真实 minimp3（`Middlewares/Third_Party/minimp3/`），喂入 `fixtures/` 里的小 MP3 夹具，只观察 `Service_Playback_Advance()` 的回报与 `Service_Playback_ReadFrame()` 取出的 PCM。不链接 FreeRTOS、HAL 或 Platform；输出管线与假 Platform 音频由后续票补入本目录。

三个 CTest：

| CTest | 覆盖 |
|---|---|
| `playback_service_stream` | 跨块喂入不丢帧（四个夹具 × 五种分块，PCM 哈希逐字节一致且非静音）；W_min 门槛；输入结束后不足 W_min 的余量原样解完；窗口接收量与结束后拒绝交入；新流丢弃旧窗口；流尾截断残余静默丢弃 |
| `playback_service_sync` | R 在垃圾字节上触发且保持；少于 R 的垃圾不触发；真实夹具带垃圾头不误判；流中垃圾后重同步；采样率不同的帧丢弃；异频帧计入 R |
| `playback_service_info` | 首帧采样率与声道数（只回报一次、落在带 PCM 的推进上）；总时长来自 Xing、VBRI 或按字节数估算；已解码时间逐帧累计、单调、钳在总时长；重开同一首从头计 |

运行（仓库根目录）：

```powershell
./scripts/test-host.ps1 -Module playback_service
```

## 夹具

`fixtures/` 的四个 MP3 与 `generate_fixtures.py` 的来源、规格和期望数字见 [fixtures/README.md](fixtures/README.md)。垃圾字节不放文件：测试用固定种子的线性同余发生器现场生成，结果与平台无关。

## 不覆盖

PCM 半区、预填、欠载、静音、排空和同曲恢复的半区交换属于输出管线，随其票补入；解码耗时、栈水位和出声只能上板，Host PASS 不是板级验收（`hw:pending`）。
