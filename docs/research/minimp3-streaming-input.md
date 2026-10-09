# minimp3 流式输入合同：帧边界、同步与输入余量

- 票：[#46](https://github.com/immorcoding/mp3_project/issues/46)（wayfinder:research），父图 #44，阻塞 #49「块池与跨块解码输入」
- 调研日期：2026-10-08
- 上游版本：lieff/minimp3 master 提交 `ea99364f61c14656440e8d77e9c233ccf3124633`（提交时间 2026-07-27T17:59:32Z，标题 "Don't enable ARMv6 features for ARMv6-m"）。读取 `minimp3.h`、`minimp3_ex.h`、`README.md`，均为该提交的原始内容。下文行号均指该提交。
- 适用：ADR-0017（分层、不用堆）与 ADR-0019（scratch 位于任务栈）。本文只给事实与建议，不修改 ADR、shape 规则或代码。

## 1. 结论摘要

1. **输入余量**：非自由格式帧，窗口须包含「帧长（含 padding）+ 4 字节下一帧头」，否则候选帧被拒绝（【源码】`mp3d_find_frame` 1694–1700、`mp3d_match_frame` 1657–1669；【实测】S2）。工程建议窗口不小于 2308 字节（`MAX_FREE_FORMAT_FRAME_SIZE + HDR_SIZE`），可覆盖全部标准帧（最大 1729 字节）。
2. **不足时整窗丢弃**：候选不能被确认时，返回 0 样本，`frame_bytes` 等于整个输入窗口，半帧被丢弃，解码器状态被清零（`memset`，1728–1731）。README 139–141 称 `frame_bytes == 0` 为「数据不足」，与源码不符（【实测】S2、S3、S15）。
3. **返回与推进**：返回值为每声道样本数（MPEG-1 L3 与 L2 为 1152，MPEG-2/2.5 L3 为 576，Layer I 为 384）。调用方每次都按 `frame_bytes` 推进，包括返回 0 的情形。`frame_offset` 为帧头前跳过的字节数，仅在找到帧时写入。
4. **容器数据由文件层处理**：低层解码器不解析 ID3、Xing、VBRI。ID3v2 只靠同步搜索跳过，无长度校验；尾部 ID3v1 会吞掉最后一帧（【实测】S5）；APEv2 裁剪比规范多 32 字节（【实测】S11，并已对照 mutagen 源码）；Xing 帧被当作静音音频解码（【实测】S12）；VBRI 完全不识别。这与 ADR-0017 第 6 条一致。
5. **坏数据**：side info 非法时整帧跳过，并在下一次调用时清零状态（【实测】S6）。储备不足时该帧输出 0 样本，但仍保存其主数据供后续帧引用（【实测】S7b）。不做 CRC 校验。Huffman 段的越界安全未做模糊测试（【推断】）。
6. **栈与堆**：`mp3dec_t` 6668 字节。`mp3dec_scratch_t` 在 ARM 上为 16236 字节，与 ADR-0019 一致。`mp3dec_decode_frame` 栈帧为 17312 字节（`-Os`），加上最深已测被调链，约 17.6 KB，高于 ADR-0019 的「栈下限 16240」（【实测】+【推断】）。低层 API 无堆分配。
7. **高层 `mp3dec_ex_*` 不适合本项目**：`open_cb` 分配 128 KiB IO 缓冲；索引初始分配 64 KiB；无 Xing 且未设 `MP3D_DO_NOT_SCAN` 时整流扫描；精确 seek 需要索引。当前堆为 32 KiB（ADR-0019）。建议只使用低层 API。

## 2. 方法与证据等级

- 【源码】：在上游源码中读到的行为，附文件、函数或宏及行号。
- 【实测】：探针程序直接调用 `mp3dec_decode_frame` 或 `mp3dec_ex_open_cb`。主机为 x86-64，MinGW gcc 15.1.0，`-O1`。输入为手工构造的静音帧（见 2.1），只验证帧界、窗口、推进、储备与错误路径，不验证音质。
- 【推断】：由源码推出、未运行验证的结论，文中逐条标注。
- ARM 尺寸与栈：`arm-none-eabi-gcc` 15.2.1，`-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb`，分别使用 `-Os`（Release）与 `-O0 -g3`（Debug），与 `cmake/gcc-arm-none-eabi.cmake` 的 TARGET_FLAGS 及 Release/Debug 设置一致。仅做编译、`-fstack-usage` 与汇编输出，未链接，未上板运行。

### 2.1 合成帧

MPEG-1 Layer III，128 kbps，44.1 kHz，立体声，无 padding，无 CRC。帧头为 `FF FB 90 00`，帧长 `1152×128000/8/44100` 取整为 417 字节，其余字节清零（side info 与主数据全为 0，解码结果为静音）。探针源码未入库，按本节描述即可复现（见第 8 节）。

## 3. 逐条回答

### 3.1 每次调用需要的连续字节

**帧长上限**

【实测】枚举全部以 `FF` 开头的 4 字节头，其中 `hdr_valid` 为真的有 645120 个，自由格式 43008 个。取「帧长 + padding」的最大值如下：

| 版本 | Layer III | Layer II | Layer I |
|---|---|---|---|
| MPEG-1 | 1441（`FF FA EA 00`） | 1729（`FF FC EA 00`） | 676（`FF FE EA 00`） |
| MPEG-2 | 721（`FF F2 EA 00`） | 1441（`FF F4 EA 00`） | 772（`FF F6 EA 00`） |
| MPEG-2.5 | 1441（`FF E2 EA 00`） | 不接受（`hdr_valid` 只允许 L3） | 不接受 |

【源码】`MAX_L3_FRAME_PAYLOAD_BYTES` 的注释要求「>= 1440」（minimp3.h:54）。`MAX_FREE_FORMAT_FRAME_SIZE` 为 2304，注释为「more than ISO spec's」（minimp3.h:49）。帧长公式见 `hdr_frame_bytes`（301–309），padding 见 `hdr_padding`（311–314）。

**同步确认的前瞻**

- 【源码】候选帧接受条件（`mp3d_find_frame` 1671–1706）：帧头合法；候选帧加 padding 完整位于窗口内；随后 `mp3d_match_frame`（1657–1669）逐个检查后续帧头，最多 `MAX_FRAME_SYNC_MATCHES` = 10 个（minimp3.h:50–52，可宏覆盖）。若窗口在第 k 个帧头之前结束，只要至少匹配了一个后续帧头即接受（1663–1664）。
- 【实测】S2：「帧 + 3 字节」被拒绝，整窗丢弃；「帧 + 4 字节」被接受。
- 【源码】特例：候选位于窗口起点，且窗口恰好为一帧时直接接受（1696）。README 147–149 称其为「已分离帧流」的情形。【实测】S1 的第三次调用，以及 S2 中 k=0 的情形。
- 【源码】快速路径（1720–1727）：上一帧头仍兼容时，只需检查下一帧头存在且匹配。下一帧头不完整则 `frame_size = 0`，转入慢速路径并清零状态（1728–1731）。
- 【实测】S15：前导垃圾 J = 600 字节时，窗口为 1019 会丢帧，为 1021 则正常。即所需窗口为 `J + 帧长 + padding + 4`。
- 【源码】自由格式（bitrate index 为 0）：帧长未知，需在窗口内搜索 k（4 ≤ k ≤ 2303），使位置 k 与 2k 处的帧头均兼容（1681–1693）。【实测】S8：k = 300 时，窗口 603、604 失败，605 成功。所需窗口约为 2k + 5 字节，k 上限 2303 时约 4.6 KB。README 120–121 称「至少 3 帧」，与 2k + 5 的计算一致（两帧长加第三帧头）。

**结论**（【推断】，工程建议）

- 硬下限：`帧长 + padding + 4` 字节（窗口起点即帧头，且无前导垃圾）。
- 通用窗口：不小于 2308 字节，覆盖全部标准帧（最大 1729 + 4）。存在前导垃圾 J 时需满足 `J + 帧长 + 4 ≤ 窗口`。
- 上游对齐：minimp3_ex 以 16 KiB 为补读阈值（`MINIMP3_BUF_SIZE`，minimp3_ex.h:27，注释称约 10 帧），见 `mp3dec_load_cb` 369–381、`mp3dec_ex_read_frame` 915–931。

### 3.2 返回值、frame_bytes、frame_offset 与指针推进

| 项 | 含义 | 依据 |
|---|---|---|
| 返回值 | 找到并解码帧时为 `hdr_frame_samples(dec->header)`：MPEG-1 L3 与 L2 为 1152，MPEG-2/2.5 L3 为 576（单颗粒），Layer I 为 384。未找到、被丢弃或储备不足时为 0 | minimp3.h:1805；`hdr_frame_samples` 296–299；README 128–134 |
| 样本布局 | 每声道样本数。输出为交织格式（L, R, L, R…），类型为 int16（`mp3d_sample_t`） | `mp3d_synth_granule` 1641 行 `pcm + 32*nch*i`；`mp3d_synth_pair` 1473 行 `pcm[16*nch]`；typedef 31 行 |
| 单帧 PCM 缓冲 | 2304 个 int16，即 4608 字节（立体声 1152 × 2） | minimp3.h:11 |
| frame_bytes | 找到帧时为 `i + frame_size`，`i` 为帧头前跳过的字节数。未找到时为整个输入长度 `mp3_bytes` | 1741；1731–1735 |
| frame_offset | 仅在找到帧时写入，值为 `i`。未找到时不写，保留调用方原值 | 1742 |
| frame_bytes == 0 | 仅表示输入为空 | 【实测】S3（窗口为 0） |

- 推进规则：`mp3 += info.frame_bytes; mp3_bytes -= info.frame_bytes;`，无论返回值是否为 0 都执行。返回 0 且 `frame_bytes > 0`，表示该段被跳过或丢弃，可能是垃圾、坏帧，或不足窗口的整段。
- 【实测】S10：1000 字节零值前导加 2 帧，返回 1152，`frame_bytes` 为 1417，`frame_offset` 为 1000。只有零值时返回 0，`frame_bytes` 为 1000（S10b）。
- README 与源码不符：README 139–141 称「0 样本且 frame_bytes == 0 为数据不足」。源码与实测均表明，数据不足时 `frame_bytes` 等于窗口长度（S2 中 k = 1、3；S3 中 300、416；S5）。因此调用方不能用 `frame_bytes == 0` 判断需要补读，只能依靠 3.1 的窗口条件预防。
- 【源码】`pcm == NULL` 时只解析帧头、返回 `hdr_frame_samples`，不解码，也不更新储备（1748–1751）。【实测】S7c：扫描一帧后，解码下一帧（main_data_begin = 8）返回 0 样本。
- 【源码】`mp3dec_init` 只把 `header[0]` 置 0（1708–1711）。下一次调用因快速路径不成立而整体清零（1728–1731）。README 145 称可以不调用它。

### 3.3 不完整帧、跨缓冲尾部、ID3v2、尾标签、Xing/VBRI/Info

| 情形 | 低层 `mp3dec_decode_frame` | `minimp3_ex`（高层） |
|---|---|---|
| 不完整帧（窗口在帧中间结束） | 返回 0，`frame_bytes` 为窗口长度，整段丢弃（【实测】S3） | 通过补读保证阈值以上的余量（369–381、457–468、915–931 行） |
| 跨缓冲尾部 | 由调用方负责：把未消费的尾部与新数据拼成线性窗口。低层不保留跨帧输入，只保留比特储备（见 3.4） | 同上，`memmove` 后补读（行号同上） |
| ID3v2 头 | 不解析（minimp3.h 中无 `ID3` 字符串）。同步搜索按字节跳过。S4 中 60 字节标签（内含一个假帧头）被正确跳过，`frame_offset` 为 60。README 110–111 所称「会跳过 ID3」即指此行为，无长度校验。大标签的误同步风险未评估（【推断】） | `mp3dec_skip_id3v2`（minimp3_ex.h:161–174）：检查 `ID3` 与 syncsafe 长度，页脚标志位加 10 字节 |
| 尾部 ID3v1（`TAG`、`TAG+`） | 不处理。尾标签与末帧同窗口时，末帧被拒绝，整窗丢弃（【实测】S5：未裁剪时返回 0，`frame_bytes` 为 545） | `mp3dec_skip_id3v1`（137–146）：去掉 128 字节；若其前有 `TAG+`，再去掉 227 字节 |
| 尾部 APEv2 | 不处理（同上） | 同一函数 148–157 行，但裁剪过量，见下文 |
| Xing / Info | 不识别。首帧按普通帧解码，返回 1152 个静音样本（【实测】S12） | `mp3dec_check_vbrtag`（193–263）：检查 `Xing`/`Info`，读取帧数与 LAME 延迟、填充（249–261）。`mp3dec_load_cb` 跳过首帧并按帧数求时长（405–417），`mp3dec_load_index` 同样（645–672） |
| VBRI | 不识别 | 不识别（全文检索 `VBRI` 无结果） |
| CRC | 跳过 16 位，不校验（1754–1757） | 同低层 |

**APEv2 裁剪过量（实测与规范对照）**

- minimp3_ex.h:149–156：先减去 32 字节的 footer，再以 footer 中的 tag size 整体减去一次。tag size 包含 footer 与 items，因此无 APE 头时会多裁 32 字节。
- 【实测】S11：帧 417 字节，加 40 字节 items 与 32 字节 footer（tag size = 72），共 489 字节。`mp3dec_skip_id3v1` 后剩 385 字节，正确值应为 417。
- 【规范对照】mutagen `apev2.py`（commit `ada28b2cc92c515f3f26640a6feef6516d195872`）：183 行读取 size；196–197 行 `end = footer + 32`、`data = end - size`，说明 size 含 footer；205–207 行「exclude the footer from size」；446、454 行写入时 `len(tags) + 32`。HydrogenAudio 维基返回 403，无法直接核对，故以 mutagen 为参照实现。
- 【推断】若存在 32 字节 APE 头（footer 的 HAS_HEADER 位），算术上恰好裁到帧尾。本调研未实测。
- 结论：不要复用该 APE 分支。如需使用，按规范实现：tag size 含 footer，头部另计。

**Xing 与时长**

- 低层 API 对 Xing 帧不做任何特殊处理。使用低层时，文件层应自行决定是否跳过首帧，以及是否用帧数求时长（与 #32 相关）。
- minimp3_ex 的 Xing 检测只看 `Xing`/`Info` 在 side info 之后的位置（220–221 行），不检查 VBRI。

### 3.4 坏数据与重同步代价

- **side info 非法**：`big_values > 288`（541–544）、`block_type = 0` 的窗口切换（553–556）、`part_23_sum` 超出储备加帧体（601–604），以及 side info 读取越界（1762）。该帧不解码，返回 0 样本，`frame_bytes` 为该帧长度（帧已被定位）。`mp3dec_init` 使下一次调用整体清零。【实测】S6：第 2 帧 big_values = 511，返回 0 / 417；第 3 帧正常解码。
- **储备不足**（`main_data_begin > reserv`，`L3_restore_reservoir` 1228–1236）：该帧不解码，返回 0 样本，但 `L3_save_reservoir`（1212–1226）仍保存该帧主数据，供后续帧引用。【实测】S7b：丢弃第 1 帧后，第 2 帧返回 0，第 3 帧正常。
- **主数据损坏**：没有 CRC 校验，无法检测。Huffman 解码（`L3_huffman` 742–877）以位缓存直接读取字节（`CHECK_BITS`，767 行），段内循环受 `big_values`、sfb 表与 `BSPOS > layer3gr_limit`（861 行）约束。源码中段内读取没有显式的字节边界检查，安全性依赖上述约束。本调研未做越界测试（【推断】）。
- **重同步代价**：
  - CPU：扫描按字节进行。每个位置先做 `hdr_valid`，合法候选再做最多 10 次帧头链校验。自由格式帧长未知时，每个候选最多要做 2303 次 `hdr_compare`（【推断】，未计时）。
  - 状态：每次进入慢速路径都会 `memset` 整个 `mp3dec_t`（6668 字节），包括 IMDCT 重叠、QMF 状态与储备（1730 行）。输出是否出现可闻的不连续，未测（【推断】）。
  - 数据：候选帧之前的字节全部丢弃。候选帧本身需要完整窗口（见 3.1）。
- 【源码】`get_bits` 在越界时返回 0 且不读取（253–254 行），这是 side info 层唯一的显式越界保护。

### 3.5 栈、scratch 与堆

**结构体尺寸**（【实测】，ARM Cortex-M7，`-Os`；括号内为 x86-64 主机值，仅作对照）

| 类型 | ARM | 主机 | 说明 |
|---|---|---|---|
| `mp3dec_t` | 6668 | 6668 | 无指针，可静态持有（minimp3.h:18–23） |
| `mp3dec_scratch_t` | 16236 | 16256 | 栈上局部变量，含 `maindata[511 + 2304]`（232–239） |
| `L3_gr_info_t` | 28 | 32 | minimp3.h:223–230 |
| `L12_scale_info` | 900 | 900 | Layer I/II 分支的局部变量（1783 行） |
| `mp3dec_frame_info_t` | 24 | 24 | minimp3.h:13–16 |
| `mp3dec_ex_t` | 11424 | 11456 | 含 `buffer[2304]`（minimp3_ex.h:82） |
| `mp3dec_index_t` | 12 | 24 | minimp3_ex.h:57–61 |
| `mp3dec_io_t` | 16 | 32 | minimp3_ex.h:66–72 |

**`mp3dec_decode_frame` 的栈帧**（【实测】`-fstack-usage`，ARM）

| 配置 | 本函数栈帧（字节） | 说明 |
|---|---|---|
| `-Os`（Release，全部 Layer） | 17312 | 含 16236 字节 scratch 与 900 字节 `sci` |
| `-O0 -g3`（Debug，全部 Layer） | 17216 | |
| `-Os`，`MINIMP3_ONLY_MP3` | 16608 | 去掉 Layer I/II，少 704 字节 |

- 最深已测被调链（`-Os`）：`mp3dec_decode_frame`（17312）→ `mp3d_synth_granule`（312）→ `mp3d_synth_pair`（8），合计 17632 字节。其它直接被调函数较浅：`mp3d_find_frame` 64、`L3_imdct36` 104、`L3_read_side_info` 56。
- `-O0` 下按 `.su` 中被调函数的帧长相加，约 17.5 KB（【推断】，调用关系未逐一核对）。
- 以上数字不含任务函数自身的栈帧、FreeRTOS 上下文与中断帧（【推断】，需另计）。

**堆**

- 【源码 + grep】`minimp3.h` 仅包含 `<stdlib.h>` 与 `<string.h>`，没有 `malloc`、`realloc`、`free` 调用。低层解码路径不分配堆，只使用 `memset`、`memcpy`、`memmove`、`memcmp`。
- 【源码】无 libm 调用（`sqrt`、`cos`、`pow` 等均不出现）。`L3_ldexp_q2` 为自定义函数。
- 【源码】SIMD：Cortex-M7 没有 NEON，`HAVE_SIMD` 为 0，使用标量单精度浮点（FPv5-D16）。`HAVE_ARMV6` 为 1 时使用 `ssat` 内联汇编（minimp3.h:194–204），编译通过。

### 3.6 高层 `mp3dec_ex_*` 是否适合嵌入式

| 功能 | 堆需求 | 证据 |
|---|---|---|
| `mp3dec_ex_open_cb` | IO 缓冲 128 KiB，`malloc` | 【实测】malloc(131072)；minimp3_ex.h:1025–1028 |
| 索引 | 首次分配 4096 项 × 16 字节 = 64 KiB，之后按倍数增长，使用 `realloc` | 【实测】realloc(65536)；minimp3_ex.h:676–686；`mp3dec_frame_t` 51–55 |
| 整流扫描 | 无 Xing 且未设 `MP3D_DO_NOT_SCAN` 时，读取整个流并建立索引。扫描时还要预解码帧直到解出样本，最多 255 帧 | 【实测】50 帧时 io 读取 20850 字节、索引 50 项；minimp3_ex.h:645–698 |
| `MP3D_DO_NOT_SCAN` | 只分配 IO 缓冲，样本数未知 | 【实测】S13 |
| Xing 首帧 | 只分配 IO 缓冲，样本数取自帧数（50 × 1152 × 2 = 115200） | 【实测】S13；641–672 |
| `mp3dec_load_cb`、`mp3dec_load_buf` | 整段 PCM 解码到堆，`realloc` 增长 | minimp3_ex.h:431–438、450、517 |
| stdio 分支（非 POSIX、非 Windows） | 整个文件读入堆 | minimp3_ex.h:1256–1297（malloc 在 1284） |
| 释放 | `mp3dec_ex_close` 释放 IO 缓冲与索引 | minimp3_ex.h:1368–1381 |
| 分配器替换 | 没有 `MP3D_MALLOC` 之类的替换宏（grep 无结果），只能全局替换 malloc | grep |

- **seek**：`MP3D_SEEK_TO_SAMPLE` 需要索引。索引未建时，`mp3dec_ex_seek` 会整流扫描（774–795）；随后预解码 2 帧补充储备（809–851）；还需要 io 的 read 与 seek 回调。字节级 seek 不保证样本精度（README 151–158）。
- **结论**：当前堆为 32 KiB，ADR-0019 已记为约 17.5 KB 占用。仅 IO 缓冲就超过 4 倍。`mp3dec_ex_*` 不适合本项目。应使用低层 `mp3dec_decode_frame`，并在文件层自行实现容器解析（ID3v2、尾标签、Xing、时长）。这与 ADR-0017 第 5、6 条一致。

## 4. 与仓库现有决定的核对

- **ADR-0017 第 3 条**（状态与输出缓冲静态持有，不用堆）：成立。低层 API 无堆。`mp3dec_t` 为 6668 字节，PCM 输出为 4608 字节，建议静态持有。
- **ADR-0017 第 6 条**（文件层跳过 ID3v2，解码器只接收帧流）：本调研支持这一决定。ID3v2 仅靠同步搜索跳过，尾标签会吞掉末帧，文件层必须处理。
- **ADR-0019 scratch 16236 字节**：与实测一致（ARM，`-Os`）。
- **ADR-0019「栈下限 16240 字节」**：低于实测。解码路径约 17.6 KB（`-Os`），多出约 1.4 KB，主要来自 Layer I/II 分支的 `L12_scale_info`（900 字节）及其它局部变量。ADR 已说明实施时要补足这部分余量。建议实施票按不低于 17632 字节加任务余量定栈，并在板上复核水位。本调研不修改 ADR。
- **0.6.0 只做 MP3**：若启用 `MINIMP3_ONLY_MP3`，栈帧从 17312 降到 16608，并去掉 L1/L2 代码。此时遇到 L2 帧会返回 0 样本，但仍推进 `frame_bytes`（1780–1781）。代码体积未测量。

## 5. 对 #49「块池与跨块解码输入」的建议（【推断】）

1. 解码输入必须是线性连续内存。跨块时，把未消费的尾部与下一块头部拷贝到线性中转窗口（参照 `mp3dec_ex_read_frame` 的 memmove 与补读，915–931 行），不能做分散聚集。
2. 补读阈值：剩余字节小于 W_min 且未到 EOF 时补读，再调用解码。建议 W_min = 2308；若与上游对齐，取 16 KiB。
3. 不向解码器提交半帧，也不提交「帧 + 不足 4 字节」的窗口。这种调用会丢弃整窗并清零状态（见 3.1、3.2）。
4. 每次调用都按 `frame_bytes` 推进。返回 0 样本且 `frame_bytes > 0` 视为丢弃，需要计数。
5. 不跳过已交给解码器的帧，否则储备不一致（见 S7b）。错误恢复后，应预期若干帧静音。
6. EOF：文件层给出精确的音频末尾，即去掉 ID3v1（128 或 227 字节）与 APEv2（按规范大小）之后的范围。最后一帧的窗口须从该帧开头开始，且恰好结束（特例，1696 行）。
7. 内存：解码器状态 6668 字节与 PCM 输出 4608 字节静态持有。解码任务栈不低于 17632 字节（`-Os`）加任务余量。中转窗口 2308 至 16384 字节。窗口只由 CPU 访问，可放 DTCM（HWD-6 允许「读缓冲」放 DTCM）；DMA 缓冲仍须在 AXI SRAM 或 SDRAM。
8. 自由格式：若 #32 决定 0.6.0 只支持常规码率，文件层可以拒绝 bitrate index 为 0 的流；否则窗口至少需要 4.7 KB。
9. Xing/Info 与 VBRI：低层会解出一帧静音。若需要精确时长或去掉首帧，由文件层处理。minimp3_ex 的 `mp3dec_check_vbrtag` 可参考，但其 APE 分支不可复用。VBRI 不支持。归 #32 决定。

## 6. 未确认与风险

- **未上板**：解码耗时、栈水位、中断与 DMA 下的行为均未测，需标记 `hw:pending`。
- **未用真实音频**：合成静音帧不覆盖真实 side info（窗口切换、短块、强度立体声、MS 立体声），也不验证音质。
- **MPEG-2/2.5 Layer III**（576 样本、单颗粒）仅经源码阅读，未实测。
- **坏数据内存安全**：未做模糊测试。`L3_huffman` 的段内读取依赖 sfb 表与 `BSPOS` 的约束，未经验证。
- **APEv2 带头**：算术上恰好裁到帧尾（【推断】，未实测）。无头时多裁 32 字节（已实测）。
- **VBRI**：完全不识别（源码确认）。
- **`mp3dec_iterate_cb`**：窗口内连续垃圾长度达到剩余窗口时，可能在 600–607 行提前结束（【推断】，未实测）。本项目不使用该回调。
- **README 与代码不一致**：错误码（README 189–190 与 minimp3_ex.h 32–36 不同）；`seek_method` 与代码中的 `flags`；以及 3.2 所述的「frame_bytes == 0 为数据不足」。以源码为准。
- **版本固定**：ADR-0017 要求固定到具体提交。本文基于 `ea99364`。升级上游须复测 3.1 的窗口常数与 3.5 的栈数字。

## 7. 证据索引

以下行号均为提交 `ea99364f61c14656440e8d77e9c233ccf3124633`。

| 主题 | 位置 |
|---|---|
| 结构体与常量 | minimp3.h:11（样本数）、13–16（frame_info）、18–23（mp3dec_t）、31（样本类型）、36（原型）、49–56（帧长与同步常量）、223–239（gr_info、scratch） |
| 帧头函数 | minimp3.h:264（hdr_valid）、273（hdr_compare）、281–288（码率）、290–294（采样率）、296–299（样本数）、301–309（帧长）、311–314（padding） |
| 比特读取 | minimp3.h:248–262（get_bits，越界返回 0） |
| side info | minimp3.h:484–607（读取与检查） |
| 比特储备 | minimp3.h:1212–1236（save、restore） |
| Huffman 与解码 | minimp3.h:742–877、1238–1272 |
| 同步搜索 | minimp3.h:1657–1669（match_frame）、1671–1706（find_frame） |
| 解码入口 | minimp3.h:1708–1711（init）、1713–1806（decode_frame） |
| 合成与交织 | minimp3.h:1451–1474（synth_pair）、1629–1655（synth_granule） |
| 高层 ID3 与 Xing | minimp3_ex.h:137–159（skip_id3v1）、161–174（skip_id3v2）、193–263（check_vbrtag） |
| 高层加载与索引 | minimp3_ex.h:313–525（load_cb）、566–639（iterate_cb）、641–698（load_index） |
| 高层 seek 与读取 | minimp3_ex.h:743–893（seek）、895–987（read_frame）、1015–1045（open_cb） |
| 结构体与堆 | minimp3_ex.h:38–88（类型）、1256–1297（stdio 分支）、1368–1381（close） |
| README | README.md:107–126（用法）、110–114（同步与 10 帧）、116–118（末帧与尾标签）、120–121（自由格式）、128–145（返回值表）、147–149（分离帧流）、151–158（seek）、185–190（常量与错误码） |
| 规范对照（外部） | mutagen `mutagen/apev2.py` @ `ada28b2cc92c515f3f26640a6feef6516d195872`：183、196–197、205–207、446、454 行 |

## 8. 复现

- 主机探针：`gcc -std=gnu11 -O1 -I<minimp3 目录> probe.c`，其中先定义 `MINIMP3_IMPLEMENTATION`，再包含 `minimp3_ex.h`。malloc 计数可用 `-Wl,--wrap=malloc,--wrap=realloc,--wrap=free` 加上 `__wrap_*` 实现。
- ARM 编译：`arm-none-eabi-gcc -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -Os -fstack-usage -c probe_arm.c`。加 `-DMINIMP3_ONLY_MP3` 可得到 MP3 专用版本。尺寸可通过 `-S` 输出中的 `.word` 常量读取。
- 合成帧与 S 编号测试见 2.1 与第 3 节。
