# Storage 音频流主机测试

本目录通过 `storage_audio_tag.h` 验证越过标签纯函数：ID3v2（含扩展头、footer、synchsafe 长度）、尾部 ID3v1、APEv2（带/不带 header）、头尾组合，以及标签长度越界与音频区间为空（Storage 应报 `BAD_FILE`）。只编译 `storage_audio_tag.c`，不链接 FreeRTOS、FatFs、HAL 或块池。

Storage 音频流主循环（FatFs、信箱比较、盖章、块池队列）不做主机测试，进板级验收；正确性靠播放任务按流序号过滤。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module storage_audio_stream
```
