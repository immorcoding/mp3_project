# Storage 音频流主机测试

本目录通过 `storage_audio_range.h` 验证越过标签纯函数：ID3v2（含扩展头、footer、synchsafe 长度）、尾部 ID3v1、APEv2（带/不带 header）、头尾组合，以及标签长度越界与音频区间为空（Storage 应报 `BAD_FILE`）。只编译 `storage_audio_range.c`，不链接 FreeRTOS、FatFs、HAL 或块池。

另验证 `storage_audio_range_seek_position`（跳转重开流的 `AudioOffset` 钳位）：偏移 0 落在区间起点、区间内按起点相对换算、越过区间（含 32 位最大值）钳在区间末端、空区间停在起点。PASS 证明钳位算术；`f_lseek`、剩余字节与 ENDED 的衔接在主循环里，进板级验收。

保真边界：重开流应答里的 `AudioBytes` 在 `storage_audio_probe_file`（FatFs 的 `f_lseek`、`f_size` 与音频区间）里取得，不在可主机测试的纯函数内，本目录不新建接缝，也没有断言“`AudioOffset` 非 0 时应答 `AudioBytes` 仍是整段区间长度”。这一条（以及剩余字节、ENDED 的衔接）由板测证明：S4（越尾）、O4（跳转重开流，`Stream` INFO 的 `off≠0` 之后播放模块按整段区间映射、重开仍成功）。

第二个测试程序 `storage_audio_result_tests`（CTest `storage_audio_result`，#211）通过 `storage_audio_result.h` 验证音频流对文件调用结果的分类，只编译 `storage_audio_result.c`，类型取自共用 FreeRTOS 桩的头（不链接桩）：读块结果 → 继续 / 停住 / 读错误（`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误，流停住等卷事件，不写 `READ_ERROR`；其他错误表示卡还在，立刻报读错误；读到 0 字节仍是读错误）；开流和探测结果 → 开启结果码（卷不可用与无文件系统为 `NOT_READY`，超时为 `IO_ERROR`，参数非法为 `BAD_FILE`，其余打开失败为 `BAD_FILE`、探测失败为 `IO_ERROR`，与原映射一致）。期望值来自 #195 第 9 条与 #202 D 节。PASS 只证明分类；“停住后不空转、卷事件到来时写 `CARD_REMOVED`”在主循环里，不做主机测试，进板级验收（R3.1、R4.5）。

Storage 音频流主循环（FatFs、信箱比较、盖章、块池队列）不做主机测试，进板级验收；正确性靠播放任务按流序号过滤。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module storage_audio_stream
```
