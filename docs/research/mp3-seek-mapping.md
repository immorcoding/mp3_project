# MP3 时间到文件偏移的映射与跳转后的解码重同步

- 票：[#161](https://github.com/immorcoding/mp3_project/issues/161)（wayfinder:research），父图 [#160](https://github.com/immorcoding/mp3_project/issues/160)，阻塞 [#166](https://github.com/immorcoding/mp3_project/issues/166)。
- 调研日期：2026-10-30。
- 分支：`research/mp3-seek-mapping`，自 `origin/main`（bcc086e）切出。只留存结论，不合并，不开 PR。
- 与 #46 的关系：帧界、同步窗口、ID3 与尾标签、`mp3dec_ex_*` 的结论见 [minimp3 流式输入合同](https://github.com/immorcoding/mp3_project/blob/research/minimp3-streaming-input/docs/research/minimp3-streaming-input.md)（下称 #46 文档）。本文只引用其编号（如 S7b），不重复。
- 范围：只记事实与证据等级，并列出对后续决定的含义，不做取舍。

## 1. 结论摘要

1. **CBR 线性映射的误差**来自音频区间起点之前的字节（ID3v2、Info 帧）、尾部标签、解码器延迟（529 采样，约 12.0 ms），以及 26.12 ms 的帧粒度。按 128 kbps 计，10 KiB 的 ID3v2 约 0.64 s，200 KiB 的带封面标签约 12.8 s（【算术】）。
2. **TOC** 为 100 项、每项 1 字节，表示文件位置占全长的 1/256（FFmpeg、Rockbox、mutagen 的读取方式一致）。LAME 在写标签时对 VBR 与 CBR 都写 TOC，其值由每帧码率的累计和算出；FFmpeg 复用器的 TOC 由累计字节数算出（【源码】）。
3. **VBRI** 表的固定头与条目布局可由 mutagen 读出；条目语义与 scale factor 的作用**未核实**。LAME 的 `VbrTag.c` 没有 VBRI 字符串，FFmpeg 复用器只在复制时过滤 VBRI 头（【源码】）。
4. **无 TOC 的 VBR**，若只用首帧码率估算总时长，比例为 `R_avg / R_first`；仓库 D5 即如此。例：320 与 192 kbps 时总时长被低估 40%（【算术】）。
5. **minimp3 低层**遇到位储备不足的帧，返回 0 样本、不写 PCM，并仍保存该帧数据供后续帧引用（minimp3.h:1767–1777、1805）。44.1 kHz MPEG-1 立体声下，被丢弃的帧数上界为：128 kbps 2 帧，320 kbps 1 帧，32 kbps 8 帧（【推断】，见 §3.3.2 表）。
6. **被丢弃的帧不产生噪声**，因为 minimp3 不写其 PCM。第一个成功解码的帧缺少被丢弃帧的 IMDCT 重叠与 QMF 历史，表现为短暂瞬态（【推断】，未测）。
7. **同步确认**的后续帧头数由窗口长度决定：仓库 4 KiB 窗口在 128 kbps 下可比对 9 个，320 kbps 下 3 个。上游 README 建议的 10 个未必达到（minimp3.h:1657–1669，【算术】）。
8. **参照做法**：minimp3_ex 字节模式不预滚；采样模式预滚 2 帧并补足 511 字节储备，丢弃预滚输出。Rockbox 不预滚，丢弃可恢复错误的帧。Helix 库把错误帧输出清零，其示例播放器则跳过该帧。libmad 播放器丢弃失败帧。FFmpeg 不丢帧，走零填充路径（【源码】）。
9. **播放位置**：minimp3_ex 与 Rockbox 以请求的目标时间为准。FFmpeg 的 CBR 路径由落点字节推算，TOC 路径报告 1% 网格时间（【源码】）。

## 2. 方法、版本与证据等级

### 2.1 证据等级

- 【源码】：在所列版本的源码中读到的行为，附"文件:行号"。
- 【文档】：规范或文档页的内容。本轮可读的只有仓库内文档与 mutagen 的注释。
- 【算术】：由源码中的公式与常数算出的数值。未运行、未测。
- 【推断】：由上面几类推出的结论，未运行验证。
- 【未核实】：本轮找不到一手依据的内容。
- 【实测】：只引用 #46 文档的探针结果。本轮没有新增实测。

### 2.2 来源版本

| 来源 | 版本（提交） | 读取的文件 |
|---|---|---|
| minimp3（仓库 vendored） | `lieff/minimp3@ea99364f61c14656440e8d77e9c233ccf3124633` | `Middlewares/Third_Party/minimp3/minimp3.h`。与上游同名文件逐字节一致（忽略行尾 CR，本轮核对） |
| minimp3（上游） | 同上 | `minimp3_ex.h`、`README.md`。仓库内不含 `minimp3_ex.h`，见 `Middlewares/Third_Party/minimp3/README.md:8` |
| Helix MP3 | `earlephilhower/ESP8266Audio@f9e72dbb212ee16bb0e33dc2cacd5adafaad411f`（`src/libhelix-mp3/`） | `mp3dec.c`、`mp3dec.h`、`mp3common.h`。第三方副本，非 RealNetworks 原始发布 |
| libmad | `earlephilhower/ESP8266Audio@b4e76952c1168ddcf736a70e9a0a50c0725da553`（`src/libmad/`） | `frame.c`、`stream.c`、`layer3.c`、`mad.h`。第三方副本 |
| ESP8266Audio 播放器 | `@058e131b26e459b9aadcb589a50f07877f1a09fd` | `src/AudioGeneratorMP3.cpp`（libmad 版）、`src/AudioGeneratorMP3a.cpp`（Helix 版） |
| Rockbox | `Rockbox/rockbox@d54b9e6f8d6f43224aa39b94036486ac2aceee91` | `lib/rbcodec/codecs/mpa.c`（libmad 解码与 MPEG 音频 seek）。GitHub 只读镜像 |
| FFmpeg | `FFmpeg/FFmpeg@bea9d176f7d253379dfb6289b921eec295e8d16d`（master，2026-10-30 取得） | `libavformat/mp3dec.c`、`libavformat/mp3enc.c`、`libavcodec/mpegaudiodec_template.c` |
| LAME | `lameproject/lame@df1312276f1f46864755f647144d047c12fb50ea` | `libmp3lame/VbrTag.c`、`libmp3lame/encoder.c` |
| mutagen | `quodlibet/mutagen@3a29f17790b9a2bcd583f3b27931f3bdd91f26d1` | `mutagen/mp3/_util.py` |
| 仓库 | `origin/main@bcc086e` | `Service/playback/README.md`、`playback_service_decoder.c`、`playback_service_decoder_minimp3.c` |

行号以上表版本为准。ESP8266Audio 的文件路径均为 `src/...`。

### 2.3 未能访问或未采用的来源

- LAME 标签规范页 `http://gabriel.mp3-tech.org/mp3infotag.html`：两次访问都在 TLS 握手失败（`TLSV1_ALERT_INTERNAL_ERROR`）。改用 LAME 源码 `VbrTag.c`，它是该格式的编码端实现。
- ISO/IEC 11172-3、13818-3 原文：付费，未访问。`main_data_begin` 的位宽以实现为依据（§3.3.2）。
- Fraunhofer VBRI 规范：未找到可访问的一手文本。mutagen 作为读取端参照。mutagen-rs（docs.rs 源码，经 WebFetch 摘要）只读取 VBRI 的质量、字节数与帧数，未直接核对原文。
- HydrogenAudio 维基：#46 文档记录为 403，本轮未访问。
- 其它编码器（非 LAME、非 FFmpeg）的标签写入：未核实。

### 2.4 样本

仓库 `Tests/playback_service/fixtures/` 下的 4 个 MP3 由 `generate_fixtures.py` 生成，属合成夹具。本文不用它们估算误差量级。

## 3. 逐题回答

### 3.1 CBR 线性映射的误差来源（票第 1 点）

**已验证事实**

- 仓库现状：`BeginStream` 的入参是音频区间字节数（`Service/playback/README.md:12`）。总时长优先取流头帧数，缺失时按音频字节数 × 8 / 首帧比特率估算，且不修正（同文件 `:63`）。
- Rockbox 的 CBR 线性映射：`pos = newtime × (bitrate / 8)`（`mpa.c:211–212`），之后限制到 `filesize − id3v1len`（`:219`），再加上 `first_frame_offset`（`:223`）。即它显式处理了标签区的起点与尾部。
- FFmpeg 的 CBR 落点时间：`frame_duration × av_rescale(best_pos − data_offset, frames, header_filesize)`（`libavformat/mp3dec.c:684`），用到音频数据起点与 Xing 的帧数、字节数。
- 标签区（minimp3_ex，上游 ea99364）：ID3v2 按 syncsafe 长度跳过（`minimp3_ex.h:161–174`）。ID3v1 去掉 128 字节，若前有 `TAG+` 再去 227 字节（`:137–146`）。APEv2 的裁剪问题见 #46。
- 解码器延迟：minimp3_ex 取 LAME 字段 `delay + 529` 作为起始跳过量（`minimp3_ex.h:259–260`，`:661`）。FFmpeg 为 `start_pad + 528 + 1`（`mp3dec.c:267`）。Rockbox 对 Layer III 的 `mpeg_latency` 为 529（`mpa.c:56`，`:488`）。
- LAME 的 encoder delay 与 padding 各为 12 位（`VbrTag.c:414–417` 读取，`:806–808` 写入）。
- 帧结构：帧字节数 = `floor(1152 × br × 125 / sr)`，padding 对 Layer III 为 1 字节（`minimp3.h:301–314`）。MPEG-1 Layer III 每帧 1152 采样（`:296–299`）。

**【算术】数量级（未测）**

- 标签字节 × 8 / 码率 即时间偏差。128 kbps 下：ID3v2 10 KiB 为 0.64 s，200 KiB 为 12.8 s，ID3v1 128 B 为 8 ms。
- 一帧 = 1152 / 44100 s = 26.12 ms。MPEG-2 Layer III 的 576 / 22050 s 也是 26.12 ms。
- 解码器延迟 529 采样 = 12.0 ms（44.1 kHz）。LAME 的 encoder delay 另计。
- 平均帧长 = `144 × br × 1000 / sr`：128 kbps 为 417.959 字节，192 kbps 为 626.939 字节，320 kbps 为 1044.898 字节。

**【推断】**

- 纯 CBR 且无标签时，线性映射本身不引入系统性偏差。帧长在 417 与 418 字节之间交替，只在 ±1 字节内波动，平均值精确。
- 误差主要来自：音频起点的字节偏移（标签、Info 帧）、尾部标签是否计入总长、时间零点（延迟采样），以及以整帧为单位的 26 ms 粒度。

### 3.2 Xing/Info TOC 与 VBRI 表（票第 2 点）

#### 3.2.1 Xing/Info 的位置、标志与字段顺序

- 位置（相对帧起点）：MPEG-1 立体声 36、单声道 21；MPEG-2/2.5 立体声 21、单声道 13（仓库 `Service/playback/playback_service_decoder.c:18–22`；FFmpeg `libavformat/mp3dec.c:180` 的 `xing_offtbl` 加上 4 字节帧头；minimp3_ex.h:201–204 的注释）。
- 标志位：FRAMES 0x1、BYTES 0x2、TOC 0x4、VBR_SCALE 0x8（mutagen `_util.py:351–352`；minimp3_ex.h:197–200）。
- 字段顺序：标志（4）→ 帧数（4，有 FRAMES 时）→ 字节数（4，有 BYTES 时）→ TOC（100，有 TOC 时）→ VBR 质量（4，有 VBR_SCALE 时）→ LAME 扩展（minimp3_ex.h:222–261；`VbrTag.c:396–426`）。
- 标签名：`Info` 表示 CBR，`Xing` 表示其它。LAME 按 `cfg->vbr == vbr_off` 选择（`VbrTag.c:938–949`）。FFmpeg 以 `Info` 设 `is_cbr`（`mp3dec.c:188`）。
- 写入端：LAME 对非 free-format 调用 `Xing_seek_table`，因此 VBR 与 CBR 的标签都有 TOC（`VbrTag.c:912–925`）。标志写为 0x0F（`:953`）；free-format 的 TOC 为线性（`:917–919`）。每帧的 `AddVbrFrame` 在 `write_lame_tag` 开启时调用（`encoder.c:550–552`），与 VBR/CBR 模式无关。FFmpeg 复用器也写 Xing/Info 与 TOC（`libavformat/mp3enc.c:233–246`，`:461`）。

#### 3.2.2 TOC 的格式与精度

**已验证事实**

- 格式：100 项，每项 1 字节，值为位置占全长的 1/256。FFmpeg 读出时以 `av_rescale(b, filesize, 256)` 为位置、`av_rescale(i, duration, 100)` 为时间（`mp3dec.c:152–160`）。Rockbox 用 `toc_sizestep = filesize / 256`（`mpa.c:192`）。mutagen 的文档字符串为 “file offsets in percent encoded as 0-255”（`_util.py:364–366`）。
- LAME 的写入值：`seek_point = floor(256 × act / sum)`，其中 `act` 是按帧索引取出的**码率累计和（kbps）**，`sum` 为总和，封顶 255（`VbrTag.c:131–136`）。累计值由 `addVbr` 按每帧 `kbps` 累加（`:90–115`，`:167`）。容量满时压缩为一半并把采样间隔加倍（`:108–114`）。
- FFmpeg 复用器的写入值：`256 × bag[j] / size`，`bag` 存的是**累计字节数**（`mp3enc.c:305`，`:480–482`），封顶 255（`:482`）。
- 读取端 minimp3_ex 跳过 TOC 的 100 字节，不使用（`minimp3_ex.h:237–241`）。
- Rockbox 在 1% 网格内按 1/256 单位线性插值（`mpa.c:185–198`）。
- FFmpeg 的 TOC 索引按 `duration / 100` 的时间网格建立（`mp3dec.c:157`），且只在 `usetoc` 或 `fast_seek` 时建立（`:143`）。`usetoc` 默认为 0（`:692`）。

**【算术】精度**

- 一个 TOC 单位 = 文件长度 / 256 字节。S = 5 MB 时为 19.5 KB，相当于 128 kbps 下 1.22 s、320 kbps 下 0.49 s。S = 3 MB 时为 0.73 s 与 0.29 s；S = 10 MB 时为 2.44 s 与 0.98 s。
- 1% 时间网格：180 s 为 1.8 s，300 s 为 3.0 s，600 s 为 6.0 s。

**【推断】**

- LAME 的 TOC 以码率累计，对 MPEG-1 Layer III 与字节数近似成比例（帧长与码率成正比，padding 除外），因此两种编码端的定义差别很小。bag 压缩造成的取样误差未量化。
- CBR 的 Info 标签中 TOC 来自恒定码率的累计，近似线性。
- TOC 只给出 1% 网格上的字节位置，不含帧索引。

#### 3.2.3 VBRI 表（格式已验证，语义未核实）

**已验证事实**

- 位置：帧起点后 36 字节（FFmpeg `mp3dec.c:306`；仓库 `playback_service_decoder.c:28`）。
- 固定头 26 字节（mutagen `_util.py:491`）：`VBRI`（4）、版本（2，mutagen 要求为 1）、延迟（2）、质量（2）、字节数（4，偏移 46）、帧数（4，偏移 50）、表条目数（2，偏移 54）、scale factor（2，偏移 56）、条目字节宽（2，偏移 58，mutagen 只接受 2 或 4）、每条目帧数（2，偏移 60）。条目从偏移 62 起，大端（`_util.py:505–522`）。
- 仓库取帧数的偏移 50 与上述一致（`playback_service_decoder.c:29`）。
- 字段注释：`toc_scale_factor` 为 “Scale factor of TOC entries”，`toc_frames` 为 “Number of frames per table entry”（`_util.py:475–479`）。
- 读取方：mutagen 读出整张表，但不解释条目（`_util.py:522`）。FFmpeg 只取字节数与帧数（`mp3dec.c:300–316`）。minimp3_ex 不识别 VBRI（上游 `minimp3_ex.h` 全文 0 处匹配，本轮核对）。仓库只取帧数（`playback_service_decoder.c:97–102`）。mutagen-rs 只读到质量、字节数与帧数（WebFetch 摘要）。
- 写入方：LAME 的 `VbrTag.c` 中无 `VBRI` 字符串（本轮核对）。FFmpeg 复用器只在复制时过滤 VBRI 头（`mp3enc.c:372`）。其它编码器未核实。

**【未核实】**

- 条目是累计值还是分段值；scale factor 如何作用于条目；`toc_frames` 的精确含义。以上在本轮查到的所有实现中都没有被解释。

#### 3.2.4 无 TOC 的 VBR 估算误差

**已验证事实**

- Rockbox 无 TOC 的 VBR 按 `pos = newtime × filesize / length` 线性估算（`mpa.c:201`）。失败时以当前位置为基准、按平均速率回退（`:208`）。
- FFmpeg 的 fast-seek VBR 也是线性缩放，并警告 “may be imprecise”（`mp3dec.c:668–673`）。
- 仓库 D5 以首帧码率估算总时长（`Service/playback/README.md:63`）。

**【算术】与【推断】**

- 设 `R_avg` 为全文件平均码率，`R_first` 为首帧码率。总时长估算 `D_est / D = R_avg / R_first`。例：`R_first = 320` kbps、`R_avg = 192` kbps 时，`D_est` 为真实时长的 60%。
- 定位误差（一阶近似，假设 `R_avg` 近似不变）：目标时间 t 映射为字节 `t × R_first / 8`，落点时间约为 `t × R_first / R_avg(t)`，误差约为 `t × (R_first / R_avg(t) − 1)`。例：t = 150 s、码率同上时，误差约 100 s。此为示例，非实测。
- 若文件带 LAME 的 TOC，误差退化为 §3.2.2 的量化级别。无 TOC 时误差取决于码率波动幅度，本轮未在真实文件上测量。

### 3.3 从任意字节偏移送入 minimp3 `mp3dec_decode_frame`（票第 3 点）

#### 3.3.1 同步

**已验证事实**

- 快速路径：`dec->header` 有效、且下一帧头兼容时，只检查下一帧（minimp3.h:1720–1727）。否则 `memset` 整个解码器状态（1728–1731），包括 `reserv`、`mdct_overlap`、`qmf_state`，再在窗口内搜索。
- 搜索与确认：`mp3d_find_frame`（1671–1706）对每个合法候选调用 `mp3d_match_frame`（1657–1669）。后者最多比对 `MAX_FRAME_SYNC_MATCHES = 10` 个后续帧头（minimp3.h:50–52）。若窗口在第 k 个帧头之前结束，只要至少匹配一个后续帧头即接受（1663–1664）。
- 上游 README：“as many as 10 consecutive MP3 frames (~16KB)”，并警告 “may cause false sync and can produce 'squealing' artefacts”（upstream `README.md:110–114`，ea99364）。
- “同格式”的定义：MPEG 版本、层、采样率索引、自由格式标志必须一致。保护位、码率索引与 padding 不比较（`hdr_compare`，minimp3.h:273–279）。
- 其它实现：FFmpeg 的 `mp3_sync` 要求连续 3 个合法帧头（`MIN_VALID = 3`，`mp3dec.c:600`）。候选在目标附近的约 4 KiB 窗口内选取（`SEEK_WINDOW`，`:556`；`:604` 的候选范围为目标前 1 KiB 至后 3 KiB）。libmad 的 `mad_stream_sync` 只扫描 `0xFF` 与 `(b & 0xE0) == 0xE0`，不做后续验证（`stream.c:103–122`）。Helix 的 `MP3FindSyncWord` 只看 `0xFF` 与 `(b & 0xF0) == 0xF0`（12 位，`mp3common.h:61–62`；`mp3dec.c:109–118`），同样不做后续验证。

**【算术】窗口内可比对的后续帧头数**（候选之后，全部匹配时，按 `minimp3.h:1657–1669` 的循环计算）

| 码率（44.1 kHz，MPEG-1 L3） | 帧长 | 4096 B 窗口（仓库 D2） | 2308 B 窗口（仓库 W_min） |
|---|---|---|---|
| 32 kbps | 104 | 10（上限） | 10（上限） |
| 128 kbps | 417 | 9 | 5 |
| 192 kbps | 626 | 6 | 3 |
| 320 kbps | 1044 | 3 | 2 |

仓库的窗口与门槛见 `Service/playback/README.md:60–61`。

**【推断】**

- “约 10 帧”是上限，不是保证。窗口较短或码率较高时，确认的帧头数会少于 10，误同步的判别依据随之减弱。FFmpeg 选择 3 帧作为门槛，两种标准不同。

#### 3.3.2 位储备与跳转后的前几帧

**已验证事实**

- `main_data_begin` 位宽：MPEG-1 为 9 位，MPEG-2/2.5 为 8 位（minimp3.h:525、529；libmad `layer3.c:545` 为 `lsf ? 8 : 9`）。缓冲上限为 511 字节（minimp3.h:56；`mp3dec_t.reserv_buf[511]` 位于 `:22`）。
- 恢复判定：`L3_restore_reservoir` 返回 `h->reserv >= main_data_begin`（minimp3.h:1228–1236）。不足时 `success = 0`。
- 保存：`L3_save_reservoir` 每帧都执行，包括未解码的帧（`:1777`），保存未消耗的主数据尾部，最多 511 字节（`:1212–1226`）。未解码的帧 `bs.pos` 为 0，因此整段主数据都被保存。
- 输出：`success = 0` 时不解码、不写 PCM，函数返回 0（minimp3.h:1768–1776、`:1805`）。PCM 缓冲保留调用方原内容。仓库包装层在注释中写明返回 0 表示“没有可用 PCM”（`Service/playback/playback_service_decoder_minimp3.c:59`），并按 `ConsumedBytes` 推进（`:76–84`）。
- 解码路径的复位：`playback_service_decoder_reset` 只调用 `mp3dec_init`（同文件 `:44–50`），即把 `header[0]` 置 0（minimp3.h:1708–1711）。真正的清零发生在下一次解码进入慢速路径时（`:1728–1731`）。
- 坏边信息：`L3_read_side_info` 失败时调用 `mp3dec_init` 并返回 0（minimp3.h:1762–1765）。下一次调用因此清零储备（同上）。

**【算术/推断】被丢弃的帧数上界**

假设每帧 `main_data_begin` 取最大值 511，且之前的帧都未解码。则每个被丢弃的帧把储备增加 `region = 帧长 − 36` 字节（MPEG-1 立体声的主数据区；边信息 32 字节见 `minimp3_ex.h:201–204`）。第 `k = ceil(511 / region)` 帧之前的 k 帧被丢弃，第 k 帧（从 0 计）起可解码。

| 码率 | 帧长 | 主数据区 | 丢弃上界（帧） |
|---|---|---|---|
| 32 kbps | 104 | 68 | 8 |
| 48 kbps | 156 | 120 | 5 |
| 64 kbps | 208 | 172 | 3 |
| 96 kbps | 313 | 277 | 2 |
| 128 kbps | 417 | 381 | 2 |
| 160 kbps | 522 | 486 | 2 |
| 192 kbps | 626 | 590 | 1 |
| 256 kbps | 835 | 799 | 1 |
| 320 kbps | 1044 | 1008 | 1 |

padding 使帧长多 1 字节，表中结论不变。实际丢弃数取决于流中各帧的 `main_data_begin`：只有引用了跳转点之前数据的帧会被丢弃。

**【推断】**

- 被丢弃的帧不产生 PCM，因而输出的时间线会缩短，而不是出现静音段或噪声。
- 被丢弃的帧不产生“错误解码”。可解码的帧所用的储备都是跳转后连续读到的字节（慢速路径清零之后只累积），不会混入跳转前的数据。
- 第一个成功解码的帧之前，被丢弃帧的 IMDCT 重叠与 QMF 历史都不存在（minimp3.h:1768–1776 不执行；1728–1731 已清零）。其第一个 granule 的输出缺少前一段的重叠贡献，表现为短暂的幅度或瞬态偏差。量级未测。
- 与 #46 的 S7b 一致：丢弃第 1 帧后，第 2 帧返回 0、第 3 帧正常。该实测覆盖单帧丢弃；本节的表给出多帧丢弃的上界。

#### 3.3.3 参照实现在跳转后的行为

**minimp3_ex（上游 ea99364，仓库未引入）**

- 字节模式 `MP3D_SEEK_TO_BYTE`：只改写 `offset`，`cur_sample = 0`，最后 `mp3dec_init`（`minimp3_ex.h:748–759`、`:880–892`）。**不预滚**。
- 采样模式：借助索引定位（首次使用时整流扫描，`:774–795`）。`MINIMP3_PREDECODE_FRAMES = 2`（`:24`）之上继续回溯，直到累计 `to_fill_bytes = 511` 字节的主数据（`:809–851`）。随后按索引的采样数跳过（`:853–879`）。预滚帧被解码但不输出：`mp3dec_ex_read_frame` 中 `to_skip` 逐帧扣减（`:969–973`），`offset` 按 `frame_bytes` 推进（`:975`）。目标之前的输出被丢弃，目标处开始输出，**不出现静音段**。
- 首次定位时若有 Xing 标签，`indexes_built = 0`（`:714`、`:1042`），因此仍需整流扫描建索引（`:774–795`）。TOC 不被使用（`:237–241`）。
- 扫描时最多解 255 帧，以找到可解的起点（`:690–693`）。

**Rockbox（`mpa.c`，libmad 解码）**

- seek 动作：`seek_by_time`（`:398–423`）→ `get_file_pos`（`:172–224`）→ `seek_buffer`，然后 `init_mad()`（`:542`）。后者清零 libmad 的 stream、frame、synth 状态（`:144–170`）。**没有预滚**。`samples_to_skip` 只在起点（参数为 0）时设置（`:532–534`）。
- 解码循环：`mad_frame_decode` 失败且为可恢复错误时 `continue`（`:578–580`，注释 “Probably syncing after a seek”）。该帧**不输出，也不插入静音**。只有 `MAD_ERROR_BUFLEN` 走补读，其它不可恢复错误返回 `CODEC_ERROR`。

**libmad 库（ESP8266Audio 副本，b4e76952）**

- 储备不足：`layer3.c:2706–2708` 设 `MAD_ERROR_BADDATAPTR` 并令 `result = -1`，不调用 `III_decode`（`:2730–2733`）。储备仍在 `:2751` 之后被保存。
- `MAD_ERROR_BADDATAPTR = 0x0235`，`MAD_RECOVERABLE(e) = e & 0xff00`（`mad.h:684`、`:691`）。可恢复错误不回退 `next_frame`（`frame.c:468–470`），即该帧被消耗，解码器继续向后。
- `mad_frame_mute` 存在（`frame.c:500`），但库不自动调用，由调用方决定。

**libmad 播放器（ESP8266Audio `AudioGeneratorMP3.cpp`）**

- `DecodeNextFrame` 失败后 `goto retry`（`:258–271`），失败帧不输出，即丢弃。`desync()` 清空缓冲（`:186` 起）。同步用 `(buff[i+1] & 0xe0) == 0xe0` 搜索（`:155`）。
- 该播放器没有 seek API（全文只有 `:260` 的一处注释）。

**Helix 库（ESP8266Audio 副本，f9e72dbb）**

- `MP3Decode` 在 `MAINDATA_UNDERFLOW` 时调用 `MP3ClearBadFrame`（`mp3dec.c:385`，`:253–263`），把本帧输出清零，即静音。它还把本帧数据存入储备（`:382`），返回 `ERR_MP3_MAINDATA_UNDERFLOW = -2`（`mp3dec.h:98`，`mp3dec.c:386`）。
- 示例播放器 `AudioGeneratorMP3a.cpp`：`MP3Decode` 返回非零时只报告错误（`:120–124`），不设 `validSamples`，因此该帧**不输出**，也不播放清零后的缓冲。重同步由 `FillBufferWithValidFrame` 完成：先把 `buff[0]` 置 0（`:64`），再用 `MP3FindSyncWord` 从后续字节搜索（`:67`）。
- 库与示例播放器都没有 seek API。`mp3dec.h:124–130` 的导出函数中无 seek。

**FFmpeg（`libavcodec/mpegaudiodec_template.c`）**

- 储备不足时不丢帧。对缺失的 granule 把 `sb_hybrid` 清零并计算 IMDCT（`:1316–1324`），再继续解码本帧。输出内容未运行验证，只能确认代码路径。

**【推断】** 在上述实现中，“跳转后静音若干帧”不是库的强制行为。minimp3_ex 与 Rockbox 丢弃或预滚；libmad 播放器与 Helix 示例跳过。只有 Helix 库本身把输出清零，而示例播放器不播放它。

### 3.4 跳转后的播放位置（票第 4 点）

**已验证事实**

- minimp3_ex 采样模式：报告位置为请求值（`cur_sample = position`，`minimp3_ex.h:762`），实际输出由索引的 `to_skip` 对齐（`:853–879`）。字节模式下 `cur_sample` 被置 0（`:757`），库不再报告位置。
- Rockbox：`samplesdone = elapsed_ms × freq / 1000`，`set_elapsed` 用同一值（`mpa.c:417`、`:422`）。即**以映射目标为准**，之后随输出帧累加。
- FFmpeg TOC 路径：`av_index_search_timestamp` 按 `flags` 在索引中找到最近的项，报告该项的时间（`mp3dec.c:657`、`:687`）。即 1% 网格时间。
- FFmpeg CBR 路径：`timestamp = frame_duration × av_rescale(best_pos − data_offset, frames, header_filesize)`（`:683–684`）。即**由实际落点字节推算**，按平均每字节帧数线性换算。
- FFmpeg 线性 fast-seek（VBR）：报告请求的时间戳（`:673`）。

**【推断】** 能否用 Xing 帧数或 TOC 反推落点时间：

- TOC 只给出 1% 网格上的字节位置，不含帧索引，无法从字节位置精确得到帧号。
- Xing 帧数只给总帧数，只能按平均换算（FFmpeg 的做法，`:684`）。
- VBRI 表每条目有帧数（`toc_frames`），理论上可给出更细的锚点，但语义未核实（§3.2.3）。
- 精确的落点时间需要从已知锚点起逐帧计数。minimp3 低层不提供帧计数，minimp3_ex 的索引提供。

### 3.5 参照做法（票第 5 点）

- minimp3_ex：见 §3.3.3。精确 seek 依赖整流建立的帧索引。有 Xing 时索引仍需整流扫描。TOC 不使用。采样模式预滚至少 2 帧并补足 511 字节储备，丢弃预滚输出。
- Rockbox：TOC 与线性映射（§3.2.4、§3.4），无预滚，丢弃可恢复错误的帧。
- Helix：库只导出 `MP3InitDecoder`、`MP3FreeDecoder`、`MP3Decode`、`MP3GetLastFrameInfo`、`MP3GetNextFrameInfo`、`MP3FindSyncWord`（`mp3dec.h:124–130`），无 seek。示例播放器无 seek。
- libmad：`frame.c`、`stream.c`、`mad.h` 中 `seek` 零匹配（本轮核对）。seek 必须由播放器实现。
- FFmpeg（补充）：默认的 MP3 seek 把控制权交给通用索引（`mp3dec.c:676`，注释 `generic index code`）。`fast_seek` 与 `usetoc` 才使用线性或 TOC 映射。同步要求 3 个连续帧头（`:600`）。

## 4. 对后续决定的含义（不做选择）

1. **映射所需的输入**：CBR 的时间映射至少需要音频起点字节（含 ID3v2 与 Info 帧）、尾部标签长度与码率。无标签时的精度受 26.12 ms 帧粒度限制；有标签时受“标签字节 × 8 / 码率”限制（§3.1）。
2. **TOC 的可用性与语义**：LAME 与 FFmpeg 都写 TOC，语义不同（码率累计 vs 字节累计）。依赖 TOC 意味着承担 1/256 量化与 1% 时间网格（§3.2.2）。VBRI 表的语义未核实，依赖它之前需要另行核实（§3.2.3）。
3. **无 TOC 的 VBR**：误差与 `R_avg / R_first` 相关。仓库 D5 的总时长按同一比例偏差，且不修正（`Service/playback/README.md:63`）。是否修正、是否接受，属于后续决定。
4. **跳转后的丢帧量**：按 §3.3.2 表，低码率上界可达 8 帧，128 kbps 上界 2 帧，320 kbps 上界 1 帧。这些帧不产出 PCM，时间线会缩短。若需要连续时间线，需另行补充。
5. **预滚的前提**：minimp3_ex 式预滚需要能回读跳转点之前约 511 字节（至多 8 帧的主数据）。仓库的线性输入窗口只保留跳转后读入的字节，因此回读需要新的数据来源（如索引，或重新读取跳转点之前的字节）。
6. **报告位置的锚点**：可选的锚点有三种来源（请求值、TOC 网格、落点推算）。仓库 D6 “已解码时间只增不减”需要在跳转时设定锚点，锚点选择直接决定显示与 T_out 的起算（§3.4）。
7. **同步确认强度**：确认的后续帧头数随码率与窗口长度变化（§3.3.1 表）。仓库窗口下，高码率确认的帧头数明显少于 10 个。若要与上游的 10 个帧头对齐，窗口长度需与码率相关。若采用 FFmpeg 的 3 帧门槛，判别标准不同。
8. **解码器复位是惰性的**：跳转后第一次解码一定进入慢速路径并清零状态（§3.3.2）。因此不需要额外的复位调用，但依赖 `header[0]` 的副作用。
9. **首帧瞬态**：第一个成功解码帧的重叠缺失（§3.3.2）造成的偏差未测。若要求无瞬态，需要主机或板上的对比验证。
10. **时间零点**：LAME 文件的解码器延迟（529）与 encoder delay 不在仓库的映射中处理。仓库的解析器不读 LAME 扩展，只读帧数（`playback_service_decoder.c:88–93`）。这会使时间与样本的对应相差约 12 ms 加上 encoder delay。
11. **VBRI**：仓库能识别 VBRI 帧（`playback_service_decoder.c:97–102`），但只取帧数。若要读取 VBRI 表，解析需扩展，且语义需先核实。

## 5. 未确认与风险

- 未访问：LAME 规范页（TLS 失败）、ISO/IEC 原文、Fraunhofer VBRI 规范、非 LAME 与非 FFmpeg 编码器的标签写入。
- 【未核实】：VBRI 表的条目语义与 scale factor 的作用。
- 未运行：FFmpeg 的解码输出、libmad 与 Helix 的实际音频输出、丢弃帧的听感、首帧瞬态的幅度。
- 数量级均为算术或一阶近似，未在真实音乐文件上测量。仓库夹具为合成。
- Helix 与 libmad 为 ESP8266Audio 的第三方副本，与上游的差异未比对。Rockbox 的 ASF 分支（`mpa.c` 中的 `is_asf_stream`）未分析，与本题无关。
- 板级：本研究不涉及板上行为。解码耗时与栈水位仍为 `hw:pending`（沿用 #46）。

## 6. 证据索引

| 主题 | 位置 |
|---|---|
| vendored minimp3 与上游一致 | `Middlewares/Third_Party/minimp3/minimp3.h`；上游 `ea99364` 同名文件（忽略行尾 CR） |
| 仓库 D2–D6 | `Service/playback/README.md:12, 60–64, 67–71` |
| 仓库 Xing/VBRI 解析 | `Service/playback/playback_service_decoder.c:18–29, 50–103` |
| 仓库包装层 | `Service/playback/playback_service_decoder_minimp3.c:44–50, 59–61, 64–102` |
| minimp3 帧长与 padding | `minimp3.h:296–314` |
| minimp3 边信息与 main_data_begin | `minimp3.h:518–529, 606` |
| minimp3 储备 | `minimp3.h:1212–1236` |
| minimp3 同步 | `minimp3.h:1657–1706`（常量 `:50–52`；比较 `:273–279`） |
| minimp3 解码入口 | `minimp3.h:1708–1806` |
| minimp3 README 同步建议 | upstream `README.md:110–114`（ea99364） |
| minimp3_ex 标签与 Xing | `minimp3_ex.h:137–174, 193–263`（TOC 跳过 `:237–241`） |
| minimp3_ex 索引与 seek | `minimp3_ex.h:641–698, 714, 743–893, 895–987`（预滚帧数 `:24`） |
| Rockbox 解码与 seek | `mpa.c:56, 144–170, 172–224, 398–423, 532–542, 578–580` |
| FFmpeg TOC 与 VBRI（读取） | `libavformat/mp3dec.c:46, 138–161, 180, 188–189, 267, 300–316, 556–636, 638–692` |
| FFmpeg 复用器 TOC 与 Info | `libavformat/mp3enc.c:95–96, 233–246, 305, 372, 461, 480–482` |
| FFmpeg 解码储备 | `libavcodec/mpegaudiodec_template.c:1213, 1310, 1316–1324, 1472, 1505` |
| LAME TOC 与标签 | `VbrTag.c:90–115, 117–137, 167, 396–426, 806–808, 912–925, 938–953`；`encoder.c:550–552` |
| mutagen 标志与 TOC | `_util.py:351–352, 363–366, 410–414` |
| mutagen VBRI | `_util.py:457–522`（字段注释 `:475–479`） |
| libmad 储备与错误 | `layer3.c:534–545, 2617, 2687–2751`；`frame.c:448–470, 500`；`stream.c:103–122`；`mad.h:665–691` |
| Helix 同步与错误 | `mp3dec.c:109–118, 253–263, 286, 370–386`；`mp3common.h:56–62`；`mp3dec.h:96–130` |
| ESP8266 libmad 播放器 | `AudioGeneratorMP3.cpp:103, 120, 155, 186, 196–201, 239–273` |
| ESP8266 Helix 播放器 | `AudioGeneratorMP3a.cpp:63–67, 119–138` |

## 7. 复现

上游文件可按上表的提交读取，例如：

```
gh api -H "Accept: application/vnd.github.raw" "repos/lieff/minimp3/contents/minimp3_ex.h?ref=ea99364f61c14656440e8d77e9c233ccf3124633"
```

仓库内文件直接按行号读取。数量级表的算术见 §3.1 至 §3.3 的公式，未运行脚本。
