# Storage 音频流主机测试

本目录通过 `storage_audio_range.h` 验证越过标签纯函数：ID3v2（含扩展头、footer、synchsafe 长度）、尾部 ID3v1、APEv2（带/不带 header）、头尾组合，以及标签长度越界与音频区间为空（Storage 应报 `BAD_FILE`）。只编译 `storage_audio_range.c`，不链接 FreeRTOS、FatFs、HAL 或块池。

另验证 `storage_audio_range_seek_position`（跳转重开流的 `AudioOffset` 钳位）：偏移 0 落在区间起点、区间内按起点相对换算、越过区间（含 32 位最大值）钳在区间末端、空区间停在起点。PASS 证明钳位算术；`f_lseek`、剩余字节与 ENDED 的衔接在主循环里，进板级验收。

保真边界：重开流应答里的 `AudioBytes` 在 `storage_audio_probe_file`（FatFs 的 `f_lseek`、`f_size` 与音频区间）里取得，不在可主机测试的纯函数内，本目录不新建接缝，也没有断言“`AudioOffset` 非 0 时应答 `AudioBytes` 仍是整段区间长度”。这一条（以及剩余字节、ENDED 的衔接）由板测证明：S4（越尾）、O4（跳转重开流，`Stream` INFO 的 `off≠0` 之后播放模块按整段区间映射、重开仍成功）。

Storage 音频流主循环（FatFs、信箱比较、盖章、块池队列）不做主机测试，进板级验收；正确性靠播放任务按流序号过滤。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module storage_audio_stream
```
