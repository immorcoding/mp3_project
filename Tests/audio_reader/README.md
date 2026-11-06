# 音频读取任务主机测试

本目录测音频读取任务（`APP/tasks/audio_reader/`）里**能抽成纯函数的判断**，三个 CTest 程序；任务循环、FatFs 调用、信箱、块池和通知不做主机测试，进板级验收（第 4 次上板 R4.1 至 R4.12，见 [音频读取任务 README](../../APP/tasks/audio_reader/README.md)）。期望值来自 [#194](https://github.com/immorcoding/mp3_project/issues/194)、[#195](https://github.com/immorcoding/mp3_project/issues/195) 和 [#202](https://github.com/immorcoding/mp3_project/issues/202)，不用代码里的表达式重算。

测试用 `host_test_assert.h` 代替 `<assert.h>`：失败时打印文件、行号和条件，以退出码 1 结束，**不走 `abort()`**。用 Clang 加调试运行库（`/MDd`）编译时，`abort()` 会弹 “Debug Error! abort() has been called” 的模态对话框，进程挂着等人点，CTest 也被卡住；Harness 用 GCC（`MP3_HOST_CC`）编译，没有这个问题，但手工 `cmake -S Tests/audio_reader` 默认会选到 Clang，所以新增的纯函数测试都不依赖 `abort()`。手工构建请带 `-DCMAKE_C_COMPILER=gcc`，与 `scripts/test-host.ps1` 一致。

## `audio_reader_range_locate`（`audio_reader_range.h`，越过标签与跳转偏移）

保留自 0.9.0。ID3v2（含扩展头、footer、synchsafe 长度）、尾部 ID3v1、APEv2（带/不带 header）、头尾组合，以及标签长度越界与音频区间为空（音频读取任务应报 `BAD_FILE`）。只编译 `audio_reader_range.c`，不链接 FreeRTOS、FatFs、HAL 或块池。

另验证 `audio_reader_range_seek_position`（跳转重开流的 `AudioOffset` 钳位）：偏移 0 落在区间起点、区间内按起点相对换算、越过区间（含 32 位最大值）钳在区间末端、空区间停在起点。PASS 证明钳位算术；`f_lseek`、剩余字节与 ENDED 的衔接在 `audio_reader_stream.c` 里，进板级验收。

保真边界：重开流应答里的 `AudioBytes` 在 `audio_reader_probe_file`（FatFs 的 `f_lseek`、`f_size` 与音频区间）里取得，不在可主机测试的纯函数内，没有断言“`AudioOffset` 非 0 时应答 `AudioBytes` 仍是整段区间长度”。这一条（以及剩余字节、ENDED 的衔接）由板测证明：S4（越尾）、O4（跳转重开流，`Stream` INFO 的 `off≠0` 之后播放模块按整段区间映射、重开仍成功）。

## `audio_reader_result`（`audio_reader_result.h`、`audio_reader_diag.h`，失败结果加卷序号 → 写什么停止原因）

只编译 `audio_reader_result.c` 和 `audio_reader_diag.c`，类型取自共用 FreeRTOS 桩的头（不链接桩）。验证：

- **读块分类** `audio_reader_classify_read(status, got, volume_mounted, volume_sequence, handled_sequence)`：成功且有字节为继续；读到 0 字节为读错误；**卷变了**（序号和已处理的不同，换卡跳 2、31 位回绕都算，或卷已不挂）时 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 不是文件错误，流停住等卷事件，不写 `READ_ERROR`；**卷没变**（挂着且序号相同）时同样的三种返回值说明卡还在、设备出错，没有卷事件可等，立刻报读错误，不静音到 T_data；其他错误立刻报读错误；`SERVICE_BUSY` 是重试。序号只比相等，不比大小。
- **开流分类** `audio_reader_classify_open_status`：卷不可用与无文件系统为 `NOT_READY`，超时为 `IO_ERROR`，参数非法为 `BAD_FILE`，其余打开失败为 `BAD_FILE`、探测失败为 `IO_ERROR`（与 0.9.0 的映射一致）；`audio_reader_status_is_busy` 只有 `SERVICE_BUSY` 为真。
- **拔卡收尾** `audio_reader_end_on_volume_change(record, volume_sequence, handled_sequence)`：序号变了且流仍被播放任务持有（已成功开启，且没有以读错误或拔卡终止；已读完的流也算）才返回 `CARD_REMOVED`；序号没变、没有被持有的流、已经以读错误终止的都返回 `NONE`（不写）。
- **`SERVICE_BUSY` 日志节拍** `audio_reader_busy_log_due`：第 1 次和之后每满 100 次为真，500 次（约 5 s）共 6 行。
- **两行新日志的格式**：`CARD_REMOVED lat=<ms>ms seq=<前>-><后> stream=<n>`（时延不可用时 `lat=-`，数字截断，整行不超过 127 字符）和 `SERVICE_BUSY op=<open|read> pos=<n> n=<k>`；缓冲放不下时安全截断、不越界。

PASS 只证明分类和格式。“读失败后先对同一个句柄 `CloseFile` 等过本轮对账再取卷状态”“停住后不空转、卷事件到来时写 `CARD_REMOVED`”在 `audio_reader_stream.c` 里，不做主机测试，进板级验收（R3.1、R4.5、R4.6）；设备出错而卡还在的路径是 FULL 之外的故障注入场景，只有读代码核对。

## `audio_reader_pace`（`audio_reader_pace.h`，决定等多久）

不依赖 FreeRTOS。验证：没有打开的流一直等；没有空闲块一直等（哪怕就绪块再少）；就绪块不到水位（0 至 3 块）不等；正好 4 块以及以上、还有空闲块时让路 1 tick；信箱里已有新请求永远不等；`SERVICE_BUSY` 重试没到时刻就只等到时刻（开流重试时没有打开的流也不能“一直等”）、到了时刻不等、新请求仍然优先。PASS 不证明水位 4 块和让路 1 tick 在真实负载下够不够，那是 R4.1、R4.2 和 R5.4 的事。

## 保真边界

主机测试是单线程的，通过不等于并发没问题：不证明真实任务抢占与优先级继承、读取任务与卷任务同级时间片的真实交错、通知的唤醒时序、`CloseFile` 在闸前等多久、`SERVICE_BUSY` 重试的真实时刻、块池和信箱的内存可见性、栈余量。这些靠第 4 次上板。读取任务的主循环（FatFs、信箱比较、盖章、块池队列）不做主机测试；正确性靠播放任务按流序号过滤，主机只在 `Tests/playback_task` 钉住播放侧前提（ENDED 后记录变 CARD_REMOVED 立即停止）。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module audio_reader
```
