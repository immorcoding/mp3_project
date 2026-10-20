# Service_Playback

播放模块把播放任务交来的音频帧流字节解码为 PCM 帧，再经 `Platform_Audio_*` 以循环 DMA 放出，并自己管理输出管线（PCM 半区、预填、补满、欠载恢复、暂停与恢复、流尾排空）。它只回报事实（首帧信息、已解码时间、需要更多输入、同步预算耗尽、输出有进展、流尾放完、Start 拒绝、传输错误、STOP_FAILED），不认识曲目、序号、文件、块或队列。来源是 [#71](https://github.com/immorcoding/mp3_project/issues/71) 的 D1–D12 与 [ADR-0017](../../docs/adr/0017-mp3-decoder-minimp3.md)。

模块分两半：**解码半边**（`Advance`/`ReadFrame`，字节 → PCM 帧）与**输出管线**（`Pump` 及暂停、恢复、放弃，帧 → PCM 半区 → Platform）。输出管线内部复用解码半边的同一个解码步；同一条流上两条路径不可混用，`Pump` 驱动之后 `Advance`/`ReadFrame` 返回 `SERVICE_BUSY`，播放任务只用输出管线，解码半边留给主机测试和只要 PCM 的调用者。

## 公开 Interface

所有公开函数返回 `Service_StatusTypeDef`，类型前缀 `Service_Playback_`：

- `Service_Playback_Init()`：初始化静态状态；
- `Service_Playback_BeginStream()`：开始新流，带音频区间字节数（仅在流头缺失时估算总时长）；重置解码器、线性输入窗口、已解码时间与同步预算；
- `Service_Playback_Feed()`：把字节拷入线性输入窗口，经出参返回实际接收数（窗口满时可为 0，余量由调用方稍后重交）；
- `Service_Playback_FinishInput()`：标记输入读尽，此后窗口余量原样交给解码器直到耗尽；
- `Service_Playback_Advance()`：最多一次解码调用，回报 `NEED_INPUT`、`FRAME`、`SKIPPED`、`SYNC_EXHAUSTED`、`END` 与首帧信息、已解码时间；
- `Service_Playback_ReadFrame()`：把解出的一帧拷成立体声交错，单声道在此复制到左右声道；
- `Service_Playback_Pump()`：推进输出管线并返回事实（见下节）；
- `Service_Playback_PauseOutput()`：暂停输出（`Platform_Audio_Stop`），流保持；暂停态下 `Pump` 继续把空出的半区补满但不 Start；
- `Service_Playback_ResumeOutput()`：补好暂停期间空出的半区后 `Platform_Audio_Start`；同曲恢复重放暂停时 DMA 正在读的半区；
- `Service_Playback_AbandonStream()`：Stop 输出并使流失效，换曲与停止都先调用它；`BeginStream()` 在输出仍运行时也会先 Stop。

`Pump` 的返回：`SERVICE_OK`；无活动流 `SERVICE_NOT_READY`；读状态或提交半区失败 `SERVICE_ERROR`（按音频错误处理）。`PauseOutput`/`ResumeOutput`/`AbandonStream` 在 Stop 失败（Platform 已锁存 STOP_FAILED）或 Start 被拒时返回 `SERVICE_ERROR`，STOP_FAILED 锁存后 `BeginStream` 返回 `SERVICE_NOT_READY`，只有 `Init`（重启）解除。`PauseOutput` 在 Stop 已成功后读状态失败不算 Stop 失败：仍返回 `SERVICE_OK`，释放数沿用最近一次推进记下的值，读状态的故障留给下一次 `Pump` 判定，所以任务不会据 `SERVICE_ERROR` 误锁存 AUDIO_FAULT。

`playback_service_config.h` 持有窗口容量、W_min、同步预算 R 与帧缓冲尺寸；`playback_service.h` 包含它，其他模块不直接包含。

## 私有结构

- `playback_service_decoder.h`：私有解码接口（ADR-0017 第 5 条），上层只依赖“输入音频帧字节 → PCM 帧与帧信息”，不出现解码库类型；
- `playback_service_decoder_minimp3.c`：该接口的 minimp3 实现，只用低层 `mp3dec_decode_frame`，不用 `mp3dec_ex_*`；固定 `MINIMP3_NO_SIMD`、`MINIMP3_ONLY_MP3`，使主机测试与固件走同一份标量代码；
- `playback_service_decoder.c`：与解码库无关的 Xing/Info、VBRI 流头解析（首帧元数据帧识别与音频总帧数）。

## 输出路径与缓冲所有权（ARC-18）

PCM 半区是两个连续的静态半区（每个 1152 立体声帧、16 位，共 9216 B），位于 AXI SRAM 的 NOLOAD 段（HWD-6、HWD-2），首地址按 D-Cache 行对齐。所有权按时段转移：

| 时段 | 谁持有 | 规则 |
|---|---|---|
| 预填（Start 之前） | 播放模块 | 写满两个半区才 `Platform_Audio_Start`；Start 把两个半区视为已提交并 Clean，所以预填不调用 `SubmitHalf` |
| 运行中，半区被 DMA 读 | Platform | 模块不写；Platform 每读完一个半区累加释放数并唤醒任务 |
| 半区被释放，到 `SubmitHalf` 之前 | 播放模块 | 补满该半区后立即 `SubmitHalf`（Platform Clean 并标记已提交） |
| `Platform_Audio_Stop` 返回 OK 之后 | 播放模块 | 计数冻结；暂停与欠载恢复据此处理两个半区 |
| `Stop` 失败，STOP_FAILED 锁存 | Platform（直到重启） | 模块把 `Faulted` 置位后不再写任何半区，`Pump` 只回报 `StopFailed` |

半区编号：第 j 次补满（从 0 起）写半区 j % 2，并在 Platform 的第 j + 1 次释放时被读完；运行中“补满次数 = 已处理释放数 + 2”。`ReleasedHalves` 的奇偶即半区号（第 n 次释放的是半区 (n−1) 的奇偶），模块据此判断哪个半区空出、暂停时 DMA 正在读哪个半区。

`Pump` 每次推进的顺序：读 Platform 状态与故障 → 流尾放完判定 → 欠载恢复 → 补满空出的半区并提交（最多 2 个，帧解码约 2 个半区）→ 预填完成则 Start。输入不够就回报 `NeedInput` 并保留补了一半的半区，由播放任务交入更多字节后再次推进。

回报的事实分两类。**事件**只在发生的那次推进里给出：`FirstFrame`、`NeedInput`、`OutputStarted`（含欠载恢复后的重新 Start，供 T_out 起算）、`OutputProgress`（Platform 又释放了半区）、`Underruns`（本次处理的欠载次数，只供记日志）。**状态**在条件持续时每次推进重复给出，直到新流或重新 Init：`SyncExhausted`（同步预算耗尽，或输入读尽仍一帧都没解出）、`StreamEnded`、`StartRejected`（Platform 因参数拒绝 Start，读 `GetStatus().Fault` 判断不是锁存，B6 跳过）、`TransferError`、`StopFailed`（锁存，或 Start 被拒时 `Fault` 为 STOP_FAILED）。

- **预填与短文件**（B14）：两个半区都已填满，或输入读尽（读尽的部分补 0，其后半区填静音），就 Start；不足两个半区照样 Start，按流尾放完处理。
- **流尾**（B13、D10）：解码读尽后，已写入的最后半区不满的部分补 0；其后下一个半区填静音并提交；最后一个含真实数据的半区被释放（`ReleasedHalves` 到达该半区的释放序号）时 `Stop`，回报 `StreamEnded`。流恰好结束在半区边界时，下一次补满发现无数据，把上一个半区认作最后的数据半区。截断尾部残余静默丢弃。
- **欠载**（D8）：`Underruns` 增加时 `Stop`，丢弃两个半区（欠载时它们都已陈旧或已读完），按解码进度重新补满，再 `Start`；对外状态不变，已解码时间不回退，只在报告里给出次数。输入不够时停在“已 Stop、等输入”，下次推进补满后 Start。
- **同曲恢复**（D9）：暂停时记下 Platform 冻结的释放数 n。n 为奇数说明 DMA 正在读半区 1，恢复要重放它，而 `Start` 总是从半区 0 开始，所以交换两个半区的内容和种类（最后的数据半区的标记跟着内容走），再 Start。暂停期间已读完的半区在暂停态推进里补满但不提交。释放数超出补满数两个以上（连续欠载）时直接丢弃重来。重放不让已解码时间回退。
- **暂停中换曲的预填**：`BeginStream` 后先 `PauseOutput`，推进只解首帧并补满两个半区、回报首帧信息，不 Start；恢复后才出声。
- **STOP_FAILED 之后**（B10）：不再写 PCM 半区、不再 Start/Submit，只回报 `StopFailed`。
- **PCM 半区所在链接段**：`PLAYBACK_SERVICE_PCM_SECTION`（`playback_service_config.h`）为链接脚本 `STM32H743xG_flash.ld` 的独立段 `.playback_pcm (NOLOAD) >RAM`（AXI SRAM，位于 `.lvgl_large_ram_array` 之后；用户授权，链接脚本由用户手改）。HWD-2：半区内容在 `Init` 写 0，且首次 Start 之前一定被预填完整覆盖；AXI SRAM 没有 SDRAM 全容量诊断，无需避让。

## 行为约定

- **线性输入窗口**（D2）：DTCM 静态 4 KiB。字节先拷进窗口再解码，每次按 `frame_bytes` 推进（含解码返回 0 时），再 `memmove` 余量；解码器不直接读块。
- **解码门槛**（D3）：窗口低于 W_min（2308 B）且输入未结束时不调用解码。minimp3 在窗口不足时会丢弃整个窗口（[#46](https://github.com/immorcoding/mp3_project/issues/46)），门槛是正确性前提，不是优化。
- **同步预算 R**（D4）：自音频区间开头或上一个好帧起连续 R（64 KiB）字节解不出帧回报 `SYNC_EXHAUSTED`；采样率与首帧不同的帧丢弃（不出 PCM、不计入已解码时间）并计入 R。输入结束后的流尾截断残余静默丢弃，不受 R 影响。
- **首帧信息**（D5）：首个音频帧上回报采样率、声道数与总时长。Xing/Info 与 VBRI 元数据帧本身不含可听音频（minimp3 会把它们当静音帧解出），因此被识别后跳过（`SKIPPED`），首帧信息落在其后的第一个音频帧上。总时长优先取流头的帧数 × 每帧采样数，缺失时按音频字节数 × 8 / 首帧比特率估算，估算不修正；越过总时长时已解码时间钳在总时长。
- **已解码时间**（D6）：按已解出的每声道采样数与采样率累计，只增不减。
- **帧输出缓冲**（D7 的解码侧）：DTCM 静态 4608 B（1152 × 2 × 16 bit）；`ReadFrame` 与输出管线拷出时单声道复制到左右声道。D7 把“是否省去这次拷贝、直接写半区”留给板测按耗时决定，本实现保留这次拷贝。

已知的 minimp3 行为（[#46](https://github.com/immorcoding/mp3_project/issues/46)），上层不应假定更强的保证：

- 流尾残余不足 4 字节时无法确认前一帧之后仍有帧头，前一帧也会被丢弃；
- 没有解码器状态时（流首或失步之后）需要同一格式的帧连成一串才认帧，短于约 10 帧就遇到异频帧的开头会被当作噪声；
- 失步重同步后，依赖位储备的头几帧可能解出 0 个样本，计入同步预算。

## 编译期依赖

- `Service/service.h`、`Middlewares/Third_Party/minimp3/minimp3.h`（仅 `playback_service_decoder_minimp3.c` 包含）；
- `Platform/audio/platform_audio.h`（`Platform_Audio_Start`、`SubmitHalf`、`GetStatus`、`Stop`；ARC-7）；
- 不依赖 FreeRTOS、HAL 或 `Platform_Audio_SetEventCallback`：回调由 APP 播放任务外壳注册，本模块不知道它。

## 运行时请求与事件路径

由 APP 播放任务在任务上下文调用；本模块不创建任务、不使用队列、不注册回调，也没有 ISR 路径。Platform 音频的半区释放回调在 ISR 中只由播放任务外壳注册并置通知 FLAG，任务醒来后调用 `Service_Playback_Pump()`，本模块经 `Platform_Audio_GetStatus()` 读取计数，所以通知被合并或迟到都不丢失半区释放、欠载和故障事实。

阻塞性：`PauseOutput`、`AbandonStream`、欠载恢复与流尾放完内部会同步 `Platform_Audio_Stop()`（最坏约为静音斜坡加两次 DMA Abort 超时，约 14 ms）；`Start`/`ResumeOutput` 阻塞于 `Platform_Audio_Start()`（I2S 重新初始化加 DAC 就绪等待）。E8：Start 阻塞加一次补满半区须小于一个半区周期，板测确认前 `hw:pending`。

## 资源与约束

- 静态存储，位于 DTCM：`mp3dec_t`（6668 B）、线性输入窗口（4096 B）、帧输出缓冲（4608 B）、输出管线小状态；位于 AXI SRAM NOLOAD 段：两个 PCM 半区（9216 B）；不使用堆；
- 每帧 scratch 是 `mp3dec_decode_frame` 的栈上局部变量（约 17 KB，[ADR-0019](../../docs/adr/0019-freertos-static-task-stack.md)），栈预算属于调用它的播放任务；
- 解码耗时与栈水位未上板实测，`hw:pending`；44.1 kHz 下一帧参照预算约 26.1 ms；
- 以下全部为板级项，`hw:pending`：稳态欠载次数（目标 0）、欠载 Stop/Start 与暂停恢复的咔哒声和间隙、同曲恢复重放约 24 ms 的听感、Start 阻塞加一次补满半区 ≤ 12 ms（E8，超标先把 `PLAYBACK_SERVICE_PCM_FRAMES_PER_HALF` 改为 2304）、`Platform_Audio_Stop` 返回后不再回调、播完间隙。主机测试（假 Platform 音频）只证明模块的编排逻辑，不是出声验收。

## 禁止依赖

- 不包含 HAL、CMSIS、CubeMX 外设头或 Adapter 私有头；输出只经 `Platform_Audio_*`；
- 不包含 `APP/`；不认识曲目、序号、文件、块、队列；不持有 FatFs 对象；
- 不使用 `mp3dec_ex_*`；不在 ISR 中调用。

## 命名

公开 Interface 使用 `Service_Playback_*`；私有文件、函数与类型使用 `playback_service_*` 的 snake_case，与 APP 播放任务的 `playback_*` 区分。

## 主机测试

`Tests/playback_service/`：真实 minimp3、小 MP3 夹具与假 `Platform_Audio_*`（记录 Start/SubmitHalf/Stop，可注入半区释放、欠载、TRANSFER_ERROR、STOP_FAILED、参数拒绝），见该目录 README。
