# Service_Playback

播放模块把播放任务交来的音频帧流字节解码为 PCM 帧，再经 `Platform_Audio_*` 以循环 DMA 放出，并自己管理输出管线（PCM 半区、预填、补满、欠载恢复、暂停与恢复、流尾排空）。它只回报事实（首帧信息、已解码时间、需要更多输入、同步预算耗尽、输出有进展、流尾放完、Start 拒绝、传输错误、STOP_FAILED），不认识曲目、序号、文件、块或队列。来源是 [#71](https://github.com/immorcoding/mp3_project/issues/71) 的 D1–D12 与 [ADR-0017](../../docs/adr/0017-mp3-decoder-minimp3.md)。

模块分两半：**解码半边**（`Advance`/`ReadFrame`，字节 → PCM 帧）与**输出管线**（`Pump` 及暂停、恢复、放弃，帧 → PCM 半区 → Platform）。输出管线内部复用解码半边的同一个解码步；同一条流上两条路径不可混用，`Pump` 驱动之后 `Advance`/`ReadFrame` 返回 `SERVICE_BUSY`，播放任务只用输出管线，解码半边留给主机测试和只要 PCM 的调用者。

## 公开 Interface

所有公开函数返回 `Service_StatusTypeDef`，类型前缀 `Service_Playback_`：

- `Service_Playback_Init()`：初始化静态状态；
- `Service_Playback_BeginStream()`：开始新流，带音频区间字节数（仅在流头缺失时估算总时长）；重置解码器、线性输入窗口、已解码时间与同步预算；
- `Service_Playback_MapSeek()`：把目标毫秒映射成 `{AudioOffset, LandingMs, Map(TOC|LIN), Clamped}`，纯计算，首帧之后可调（见“跳转”一节）；
- `Service_Playback_ReopenStream()`：从落点重开，清空解码器、输入窗口与计数，保留总时长、采样率、TOC 等流级事实，已解码时间从落点起算；
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
- `playback_service_decoder.c`：与解码库无关的 Xing/Info、VBRI 流头解析（首帧元数据帧识别、音频总帧数，以及 Xing/Info 的 100 B TOC）。

## 跳转：毫秒到字节的映射与从落点重开

真跳转（[#172](https://github.com/immorcoding/mp3_project/issues/172) E 节、[#166](https://github.com/immorcoding/mp3_project/issues/166) 第 8–11 条、[ADR-0021](../../docs/adr/0021-seek-by-reopen-with-audio-offset.md)）把“目标毫秒 → 文件内字节偏移”放在本模块，Storage 只收一个相对音频区间的偏移。本模块提供两项能力，不接合同、不认识播放任务；它们由播放任务（[#176](https://github.com/immorcoding/mp3_project/issues/176)，见 [播放任务 README](../../APP/tasks/playback/README.md) 的“跳转”）接上：命令处理时调 `MapSeek`，Storage 开启 OK 后调 `ReopenStream`（暂停中随后 `PauseOutput`）。

- `Service_Playback_MapSeek(target_ms, &point)`：纯计算，输出 `{AudioOffset, LandingMs, Map, Clamped}`。
- `Service_Playback_ReopenStream(landing_ms)`：从落点重开。**选的是新入口，不给 `BeginStream` 加参数**：两者的清理范围不同（`BeginStream` 把一切归零并重新探测流头，重开保留流级事实），合在一个入口里要靠一个布尔参数区分两种截然相反的语义，调用点也读不出意图。

### 流级事实与口径

首帧解出后，下列事实一直保留到下一次 `BeginStream()`（`AbandonStream()`、读到 END、`ReopenStream()` 都不清它们）：总时长、首帧采样率与声道数、TOC（Xing/Info 的 flag bit 2，100 B，原样保留）、`AudioBytes`、“已探测过流头”标志、音频有效区间起点。因此 `MapSeek` 在首帧之后随时可调，不要求流仍在活动；调用者负责只在同一首曲目上重开（换曲必然经 `BeginStream`）。首帧之前、总时长未知（无流头且 `audio_bytes` 为 0）、`BeginStream` 之后都返回 `SERVICE_NOT_READY`。

**音频有效区间**：元数据帧（Xing/Info/VBRI）之后到音频区间末尾；无元数据帧时就是整个音频区间。起点 = 元数据帧被跳过那一刻窗口已推进的总字节数（含帧前跳过的字节）。线性映射与 TOC 的 1/256 刻度都铺在有效区间上。这样两种头的口径差别都不影响精度：

- #174 自写头的帧数、字节数与 TOC 只计音频帧、不含 417 B 的头帧本身——与“有效区间”一致，TOC 精确；
- 真实 LAME/ffmpeg 头的字节数含头帧（#174 已注明差 417 B）。TOC 的基准无论按音频帧还是按含头帧计，与“有效区间”的偏差至多一个头帧（417 B，64 kbps 约 52 ms，128 kbps 约 26 ms），在容差内；真实文件的 TOC 基准未用 ffmpeg 对照样本实测（推断，`hw:pending` 之外的待核事项）。

**映射**（`Map` 字段）：Xing/Info 带完整、单调（非递减、末项非零）的 TOC 时为 `TOC`，目标落在第 i 与 i+1 个百分点之间时在 `TOC[i]`、`TOC[i+1]` 间线性插值，第 100 项视为 256；否则为 `LIN`，按有效区间字节数与总时长等比。VBRI 一律 `LIN`（表条目语义未核实，#161）。TOC 全零或乱序视为不可用，退回 `LIN`。

**钳位**：目标超过总时长按总时长算；偏移最多钳到 `AudioBytes − W_min`（2308 B），此时 `Clamped` 为真。**落点** = 钳位后的偏移按同一映射反算（`TOC` 取其反插值），所以越尾时落点是流尾前约 W_min 字节对应的时间，不是总时长。落点不修正实际出声点：重开后位储备不足的帧被丢弃（0 样本），实际出声点比落点晚若干帧，见下“已测数字”。

### 从落点重开

`ReopenStream(landing_ms)` 清空解码器、线性输入窗口、同步预算计数、输入已读尽标志、帧输出缓冲与输出管线（上一条流的输出仍在运行时先 Stop，与 `BeginStream` 一致），保留上述流级事实；已解码采样数设为落点对应的采样数（向上取整，使 `DecodedMs` 不小于落点），此后照旧只增不减，越过总时长钳在总时长。保留“首帧已确认”的后果：重开后不再回报 `FirstFrame`，第一个解出的帧不被当作 Xing/VBRI 头检查，采样率不同的帧照旧丢弃。调用者随后从 `AudioOffset` 起重新交入字节。首帧之前调用返回 `SERVICE_NOT_READY`。暂停中跳转：重开后照常先 `PauseOutput()` 再推进，走现有预填路径。

### #166 第 11 条：一帧都解不出是否被判坏文件（主机核实）

坏文件（`SyncExhausted`）只有两种来源：自音频区间开头或上一个好帧起 R 字节解不出帧，或**新开的流**输入读尽仍无首帧。重开的流首帧已确认，且输入读尽后的残余不计 R，所以**重开后即使一帧都解不出，也不会被判坏文件**：读尽后按流尾放完处理，`Pump` 回报 `StreamEnded`、不回报 `SyncExhausted`（`test_clamped_tail_reopen_is_not_judged_a_bad_file` 对照：同样的垃圾字节，新流 `SyncExhausted`，重开流 `StreamEnded`）。因此钳位不依赖“至少解出一帧”来避免坏文件判定。

但“钳到 `AudioBytes − W_min` 保证至少能解出一帧”这个前提**只对帧长不超过约 W_min/2 的码流成立**：minimp3 无解码器状态时需要“帧 + 其后的帧头”才认帧，首个好帧还可能因位储备不足被丢弃。实测（一次性脚本，不在仓库；`lameenc` 生成 4 s 立体声，W_min 字节窗口取遍 3000 个起点对齐，等价于文件尾落在任意帧对齐）：128 kbps 的 32/44.1/48 kHz 全部至少 2 帧；**320 kbps 下 32 kHz 为 3000/3000 个对齐一帧都解不出，44.1 kHz 为 2352/3000，48 kHz 为 1842/3000**。提交的夹具（帧长不超过 417 B）经 `test_every_alignment_of_a_wmin_tail_decodes_a_frame` 在全部对齐下至少 1 帧。结果：高码率文件越尾跳转可能一帧 PCM 都不出，立即按流尾放完、前进到下一首——不是坏文件，但 #166 第 11 条“保证至少解出一帧”的措辞对 320 kbps 不成立，是否把钳位放大到比 W_min 更大（例如整个窗口 4096 B）留给 spec 作者定，本实现不改钳位规则。

### 已测数字（主机，夹具见 `Tests/playback_service/fixtures/README.md`）

- 落点精度（偏移处第一帧的真实时间 vs `LandingMs`）：CBR 64 kbps `LIN` 最差 27 ms（约 1 帧，容差 3 帧）；VBR 带 TOC 最差 90 ms（容差总时长/100 + 3 帧 = 278 ms）；无 TOC 的 VBR 总时长按首帧比特率估算严重偏离（20 s 的曲子估成 5 s），误差只记录，断言落点单调、不崩溃、越尾钳位生效。
- 重开后被丢弃的帧数（相对“偏移处第一帧起”的帧数）：CBR 64 kbps 0–3 帧，低码率 VBR 最多 4 帧（约 104 ms）。比 #161 的“CBR 128 kbps 至多 2 帧”推断更大，因为 64 kbps 下 511 B 位储备跨 2.5 帧、小帧 VBR 更多；#166 第 10 条“CBR 下 ≤ 约 50 ms”对 64 kbps 不成立。

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

`Tests/playback_service/`：真实 minimp3、小 MP3 夹具与 `fixtures/xing/` 的 20 s 精度夹具与假 `Platform_Audio_*`（记录 Start/SubmitHalf/Stop，可注入半区释放、欠载、TRANSFER_ERROR、STOP_FAILED、参数拒绝），见该目录 README。
