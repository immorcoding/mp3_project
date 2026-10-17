# Service_Playback

播放模块把播放任务交来的音频帧流字节解码为 PCM 帧，只回报事实（首帧信息、已解码时间、需要更多输入、同步预算耗尽、流尾），不认识曲目、序号、文件、块或队列。来源是 [#71](https://github.com/immorcoding/mp3_project/issues/71) 的 D1–D6、D11 解码部分与 [ADR-0017](../../docs/adr/0017-mp3-decoder-minimp3.md)。

本目录目前只有**解码半边**；输出管线（PCM 半区、Platform 音频、预填、欠载、静音、排空）由后续票补入，届时 `Service_Playback_Advance()` 的事实集合与本 README 一并扩充。

## 公开 Interface

所有公开函数返回 `Service_StatusTypeDef`，类型前缀 `Service_Playback_`：

- `Service_Playback_Init()`：初始化静态状态；
- `Service_Playback_BeginStream()`：开始新流，带音频区间字节数（仅在流头缺失时估算总时长）；重置解码器、线性输入窗口、已解码时间与同步预算；
- `Service_Playback_Feed()`：把字节拷入线性输入窗口，经出参返回实际接收数（窗口满时可为 0，余量由调用方稍后重交）；
- `Service_Playback_FinishInput()`：标记输入读尽，此后窗口余量原样交给解码器直到耗尽；
- `Service_Playback_Advance()`：最多一次解码调用，回报 `NEED_INPUT`、`FRAME`、`SKIPPED`、`SYNC_EXHAUSTED`、`END` 与首帧信息、已解码时间；
- `Service_Playback_ReadFrame()`：把解出的一帧拷成立体声交错，单声道在此复制到左右声道。

`playback_service_config.h` 持有窗口容量、W_min、同步预算 R 与帧缓冲尺寸；`playback_service.h` 包含它，其他模块不直接包含。

## 私有结构

- `playback_service_decoder.h`：私有解码接口（ADR-0017 第 5 条），上层只依赖“输入音频帧字节 → PCM 帧与帧信息”，不出现解码库类型；
- `playback_service_decoder_minimp3.c`：该接口的 minimp3 实现，只用低层 `mp3dec_decode_frame`，不用 `mp3dec_ex_*`；固定 `MINIMP3_NO_SIMD`、`MINIMP3_ONLY_MP3`，使主机测试与固件走同一份标量代码；
- `playback_service_decoder.c`：与解码库无关的 Xing/Info、VBRI 流头解析（首帧元数据帧识别与音频总帧数）。

## 行为约定

- **线性输入窗口**（D2）：DTCM 静态 4 KiB。字节先拷进窗口再解码，每次按 `frame_bytes` 推进（含解码返回 0 时），再 `memmove` 余量；解码器不直接读块。
- **解码门槛**（D3）：窗口低于 W_min（2308 B）且输入未结束时不调用解码。minimp3 在窗口不足时会丢弃整个窗口（[#46](https://github.com/immorcoding/mp3_project/issues/46)），门槛是正确性前提，不是优化。
- **同步预算 R**（D4）：自音频区间开头或上一个好帧起连续 R（64 KiB）字节解不出帧回报 `SYNC_EXHAUSTED`；采样率与首帧不同的帧丢弃（不出 PCM、不计入已解码时间）并计入 R。输入结束后的流尾截断残余静默丢弃，不受 R 影响。
- **首帧信息**（D5）：首个音频帧上回报采样率、声道数与总时长。Xing/Info 与 VBRI 元数据帧本身不含可听音频（minimp3 会把它们当静音帧解出），因此被识别后跳过（`SKIPPED`），首帧信息落在其后的第一个音频帧上。总时长优先取流头的帧数 × 每帧采样数，缺失时按音频字节数 × 8 / 首帧比特率估算，估算不修正；越过总时长时已解码时间钳在总时长。
- **已解码时间**（D6）：按已解出的每声道采样数与采样率累计，只增不减。
- **帧输出缓冲**（D7 的解码侧）：DTCM 静态 4608 B（1152 × 2 × 16 bit）；`ReadFrame` 拷出时单声道复制到左右声道。

已知的 minimp3 行为（[#46](https://github.com/immorcoding/mp3_project/issues/46)），上层不应假定更强的保证：

- 流尾残余不足 4 字节时无法确认前一帧之后仍有帧头，前一帧也会被丢弃；
- 没有解码器状态时（流首或失步之后）需要同一格式的帧连成一串才认帧，短于约 10 帧就遇到异频帧的开头会被当作噪声；
- 失步重同步后，依赖位储备的头几帧可能解出 0 个样本，计入同步预算。

## 编译期依赖

- `Service/service.h`、`Middlewares/Third_Party/minimp3/minimp3.h`（仅 `playback_service_decoder_minimp3.c` 包含）；
- 解码半边不依赖 Platform、FreeRTOS 或 HAL；输出管线引入后只经 `Platform_Audio_*`（ARC-7）。

## 运行时请求与事件路径

由 APP 播放任务在任务上下文调用；本模块不创建任务、不使用队列、不注册回调。当前没有 ISR 路径。输出管线引入后，Platform 音频的完成回调只由播放任务外壳注册并置通知 FLAG，不进入本模块。

## 资源与约束

- 全部为静态存储，位于 DTCM：`mp3dec_t`（6668 B）、线性输入窗口（4096 B）、帧输出缓冲（4608 B）；不使用堆；
- 每帧 scratch 是 `mp3dec_decode_frame` 的栈上局部变量（约 17 KB，[ADR-0019](../../docs/adr/0019-freertos-static-task-stack.md)），栈预算属于调用它的播放任务；
- 解码耗时与栈水位未上板实测，`hw:pending`；44.1 kHz 下一帧参照预算约 26.1 ms。

## 禁止依赖

- 不包含 HAL、CMSIS、CubeMX 外设头或 Adapter 私有头；
- 不包含 `APP/`；不认识曲目、序号、文件、块、队列；不持有 FatFs 对象；
- 不使用 `mp3dec_ex_*`；不在 ISR 中调用。

## 命名

公开 Interface 使用 `Service_Playback_*`；私有文件、函数与类型使用 `playback_service_*` 的 snake_case，与 APP 播放任务的 `playback_*` 区分。

## 主机测试

`Tests/playback_service/`：真实 minimp3 加小 MP3 夹具，见该目录 README。
