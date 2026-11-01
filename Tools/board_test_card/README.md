# 板级测试卡

[make_card.py](make_card.py) 合成一组 MP3，供 [板级验收清单模板](../../docs/agents/board-test-checklist.md) 使用。内容全部是脚本计算的确定性和弦，不含第三方录音；生成物不进 Git。

```powershell
pip install lameenc
python Tools/board_test_card/make_card.py <输出目录>
python Tools/board_test_card/make_card.py <输出目录> --copy-to I:\Music --set all
```

不带 `--set` 时列出全部样本表和子集清单，复制默认用 `all`；带 `--set X` 时只打印该子集的样本表和复制顺序（不再打印其他子集的表与清单），生成的文件仍是全套；例外是 `--set seek` 与 `--set host-fixtures`，它们只生成各自的文件。报告格式由 `python Tools/board_test_card/test_make_card_report.py` 检查（不需要 `lameenc`）。

| 复制顺序 | 文件 | 字节 | 用途 |
|---:|---|---:|---|
| 1 | `bad-zero.mp3` | 200000 | 全零坏文件，先复制才能测“首个文件是坏文件” |
| 2 | `card-01-32k-320k.mp3` | 8402400 | 32 kHz 立体声 320 kbps，3:30 |
| 3 | `card-02-44k1-320k.mp3` | 8402024 | 44.1 kHz，同上 |
| 4 | `card-03-48k-320k.mp3` | 8400960 | 48 kHz，同上 |
| 5 | `card-04-id3-1mb.mp3` | 1769555 | ID3v2.3 标签 1 MiB（APIC 帧，数据不含 0xFF）+ 44.1 kHz 192 kbps 30 s |
| 6 | `card-05-short.mp3` | 8777 | 约 0.55 s |
| 7 | `card-06-mpeg2-22k.mp3` | 40229 | MPEG-2 22.05 kHz 单声道，不支持采样率，应被跳过 |
| 8 | `bad-empty.mp3` | 0 | 空文件 |

### 曲目显示名样本（`--set names`）

给 #112 板测行 T7、T8 用。内容都是同一段可播音频（44.1 kHz 192 kbps 30 s，约 720 KB，与 `card-04` 同音频），只有文件名不同；与上表样本一样复制进卡的 `Music/` 目录（`--copy-to <盘符>:\Music --set names`），曲库相对路径为 `Music/<文件名>`。不含在 `all` 内：换一张空卡（或格式化后）单独复制，期望显示名按 [#102](https://github.com/immorcoding/mp3_project/issues/102) 规则（只去末尾 `.mp3`，大小写不限，其余原样）。

| 复制顺序 | 文件名 | 期望显示名 | 覆盖点 |
|---:|---|---|---|
| 1 | `a.b.MP3` | `a.b` | 多点号、大写后缀 |
| 2 | `a.mp3.mp3` | `a.mp3` | 主名内含 `.mp3` |
| 3 | `01 - 晴天.mp3` | `01 - 晴天` | 序号与空格原样保留 |
| 4 | `Hello 世界 Mix 混排 Song.mp3` | `Hello 世界 Mix 混排 Song` | 中英混排 |
| 5 | `繁體 龍 喆 镕.mp3` | `繁體 龍 喆 镕` | GBK 内、GB2312 外的字（T7：缺字占位框） |
| 6 | `LN` + 81 个汉字 + `.mp3` | 去掉 `.mp3` 的 245 B 主名 | 文件名 249 B，`Music/` 整串路径恰为 255 B（`SERVICE_FILESYSTEM_PATH_MAX_BYTES`）；需滚动 |
| 7–9 | 长 ASCII、纯中文、中英混排各一 | 去掉 `.mp3` | 12 px 下超过 192 px（唱盘页曲名可见宽，GUI-26），需滚动 |

- 所有字符都在固件 `_CODE_PAGE 936`（GBK）内；超出 GBK 的字符 FatFs 无法转换，文件不会入库，因此不放进样本。
- 名字合同（FAT 合法字符、`Music/` 整串路径 UTF-8 ≤ 255 B、字库外字、滚动宽度）由 `python Tools/board_test_card/test_card_names.py` 检查，不需要 `lameenc`。名字表在 [card_names.py](card_names.py)。

- 固件按 FAT 目录项顺序入库，目录顺序等于创建顺序：必须按上表顺序逐个复制，不要整夹拖拽；`--copy-to` 已按此顺序。
- `--set first-bad`（坏文件在首位）、`--set all-unsupported`（整库不可播，期望 ALL_BAD）复制子集，相对顺序不变；换子集前先格式化卡。
- 每个和弦 1 s、起止采样为 0，循环拼接，曲内没有人为咔哒声；同一 `lameenc` 版本下输出逐字节一致，上表字节数以 `lameenc` 1.9.0 为准。

- 串口日志：`capture-serial.ps1 -Port <COM口> -OutFile <路径>` 采集板子 USB CDC 日志（时间戳、自动重连），用法见[板级验收清单](../../docs/agents/board-test-checklist.md#烧录与日志)。

### ID3 标签样本（`--set tags`）

给 [#135](https://github.com/immorcoding/mp3_project/issues/135) 板级验收 I1–I6 用（#142）。音频与 `--set names` 相同（44.1 kHz 192 kbps 30 s，约 720 KB），只在文件头加不同的 ID3v2 标签；复制进卡的 `Music/` 目录（`--copy-to <盘符>:\Music --set tags`）。不含在 `all` 内：换一张空卡（或格式化后）单独复制，与 `--set names` 同写法；按下表顺序逐个复制。样本内容以 ASCII 为主，标签表在 [card_tags.py](card_tags.py)。

| 顺序 | 文件名 | 字节 | 唱盘页曲名 | Queue 曲名 / 歌手 | 名称槽 INFO：来源，读取次数 | 验收项 |
|---:|---|---:|---|---|---|---|
| 1 | `tag-1-v23-utf16le.mp3` | 721077 | `Tag Sample UTF16LE` | `Tag Sample UTF16LE` / `Test Artist A` | 标题，1 | I1 |
| 2 | `tag-2-v24-utf8.mp3` | 721039 | `Tag Sample UTF8` | `Tag Sample UTF8` / `Test Artist B` | 标题，1 | I2 |
| 3 | `tag-3-latin1-gbk.mp3` | 721031 | `测试标题 GBK`（中文正确） | 曲名与歌手 `测试歌手` 均为占位框（0.8.1 解决） | 标题，1 | I3 |
| 4 | `tag-4-apic-first.mp3` | 729107 | `Tag Sample APIC First` | `Tag Sample APIC First` / `Test Artist D` | 标题，2；无 WARN；记录耗时 | I4 |
| 5 | `tag-5-blank-title.mp3` | 721028 | `tag-5-blank-title`（文件主名） | `tag-5-blank-title` / 空 | 文件主名，原因=帧缺失，1；无 WARN | I5 |
| 6 | `tag-6-no-tag.mp3` | 720979 | `tag-6-no-tag`（文件主名） | `tag-6-no-tag` / 空 | 文件主名，原因=无 ID3v2，1；无 WARN | I6 |

- 样本 1：v2.3，编码字节 1（UTF-16 + BOM），与 [#128](https://github.com/immorcoding/mp3_project/issues/128) 实测的 42 个文件同形。样本 2：v2.4，编码字节 3（UTF-8），帧大小 syncsafe。
- 样本 3：v2.3，声明编码字节 0（Latin-1）实为 GBK。唱盘页（Noto）中文正确，Queue（Montserrat）显示占位框，不算退化。
- 样本 4：8000 B 的 APIC 位于 TIT2/TPE1 之前，TIT2 帧头在偏移 8040，越过首块 4 KiB，故读取次数为 2（首块 + 一次 seek），不读 APIC 负载。
- 样本 5：只有 TALB 与全空白的 TIT2，无 TPE1，清理后为空按“帧缺失”回退。样本 6：文件开头即 MP3 帧，无 ID3 头。
- 缺 BOM、多值、超长、非法编码、控制字符不做成样本，只在主机测试中用字节块覆盖。
- 字节数以 `lameenc` 1.9.0 为准；同参数两次生成逐字节一致。合同由 `python Tools/board_test_card/test_card_tags.py` 检查（不需要 `lameenc`）。

### 曲目元数据字形样本（`--set glyphs`）

给 [#145](https://github.com/immorcoding/mp3_project/issues/145) 板级验收 F2、F3 用（#150），0.8.2（[#146](https://github.com/immorcoding/mp3_project/issues/146)）的 V3、V4 复用。音频与 `--set names`、`--set tags` 相同（44.1 kHz 192 kbps 30 s，约 720 KB），只在文件头加不同的 ID3v2 标签；复制进卡的 `Music/` 目录（`--copy-to <盘符>:\Music --set glyphs`）。不含在 `all` 内：换一张空卡（或格式化后）单独复制，不与 `--set tags` 混在同一张卡；按下表顺序逐个复制。标签表在 [card_glyphs.py](card_glyphs.py)。

字段规则以 [#135](https://github.com/immorcoding/mp3_project/issues/135) E 节为准：多值按 NUL 拆分、去首尾空白（ASCII 空白与 U+3000）、丢弃空项，以 ` / ` 连写，不按 `/` 拆分；超过容量（252 B 含 `\0`，即 251 B）按 UTF-8 字符边界截断，不加省略号。

| 顺序 | 文件名 | 字节 | 唱盘页曲名 | Queue 曲名 / 歌手 | 名称槽 INFO：来源，读取次数 | 验收项 |
|---:|---|---:|---|---|---|---|
| 1 | `glyph-1-cjk.mp3` | 721046 | `晴天 夜曲 测试标题` | 同左 / `周杰伦` | 标题，1 | F2 |
| 2 | `glyph-2-latin-ext.mp3` | 721131 | `Beyoncé – F1® Theme ™ ’Tis “Live” …` | 同左 / `Déjà Vu ® – “Zoé” ’ ™ …` | 标题，1 | F3 |
| 3 | `glyph-3-multi-artist.mp3` | 721082 | `Multi Artist Sample` | 同左 / `Artist One / Artist Two / AC/DC / Artist Three` | 标题，1 | #146 V3 |
| 4 | `glyph-4-long-artist.mp3` | 721362 | `Long Artist Sample` | 同左 / 249 B 的 `Long Artist Name Long Artist Name … Long Artist`（`Long Artist Name ` 重复到第 249 字节，末词截在 `Artist`） | 标题，1 | #146 V4 |

- 样本 1：v2.4，编码字节 3（UTF-8）；汉字都在 GB2312 内。Queue 中文标题与歌手均应正确显示，无占位框。
- 样本 2：v2.3，编码字节 1（UTF-16 + BOM）；标题与歌手都含 `® é – ™ ’ “ ” …`（U+00AE、U+00E9、U+2013、U+2122、U+2019、U+201C、U+201D、U+2026），唱盘页与 Queue 同字体同基线。
- 样本 3：v2.4，TPE1 六项以 NUL 分隔：`Artist One`、全空白项、` Artist Two `（首尾空白）、空项、`AC/DC`、`U+3000 Artist Three U+3000`；清理后四项，其中 `AC/DC` 不被拆分。
- 样本 4：v2.4，TPE1 共 333 B（上限 251 B）：前 249 B 为 ASCII，其后是 3 B 汉字，截断点落在第一个汉字内部，所以按字符边界只留 249 B，不加省略号、不留半个字符。249 个字符远宽于 Queue 与唱盘页的可见宽度，Queue 歌手行与唱盘页歌手行（GUI-26）都用 `LV_LABEL_LONG_DOT` 截断加省略号。
- 字节数以 `lameenc` 1.9.0 为准；同参数两次生成逐字节一致。合同由 `python Tools/board_test_card/test_card_glyphs.py` 检查（标签版本、编码字节、关键码点、截断边界、不进 `all`；这些不需要 `lameenc`）。另有整文件确定性用例：同一参数两次生成，`--set glyphs` 的每个文件逐字节相同；它要编码音频，约 30 s，未装 `lameenc` 时跳过。该测试不在 `verify.ps1` FULL 内，需手动运行。

### 跳转样本（`--set seek`）

给 [#172](https://github.com/immorcoding/mp3_project/issues/172) 的跳转板级验收 S1–S4 用（#174）。四首都是 44.1 kHz 立体声、3:30（210 s）的刻度音：全零静音底，每 10 s 一声 1 kHz 短哔（100 ms，峰值 12000），整分钟（60、120、180 s）在 0.25 s 后再来一声，共 23 声，最后一声在 200 s。头由 [card_xing.py](card_xing.py) 自写（`Info`/`Xing` 的帧数、字节数、TOC 口径见[夹具 README](../../Tests/playback_service/fixtures/README.md#xing跳转精度夹具174)），刻度表在 [card_seek.py](card_seek.py)。复制进卡的 `Music/` 目录（`--copy-to <盘符>:\Music --set seek`）。不含在 `all` 内：换一张空卡单独复制，按下表顺序逐个复制。

| 顺序 | 文件名 | 字节 | 头 | 编码 | 期望现象（映射、听感） |
|---:|---|---:|---|---|---|
| 1 | `seek-1-cbr320-nohdr.mp3` | 8402024 | 无 | CBR 320 | `Seek` INFO `map=lin`；跳到刻度前约 2 s，屏幕走到刻度时听到哔声，偏差 ±0.5 s 内 |
| 2 | `seek-2-cbr320-info.mp3` | 8402441 | `Info`，flags 0x03，无 TOC | CBR 320 | `map=lin`；同上 |
| 3 | `seek-3-vbr-toc.mp3` | 850838 | `Xing`，flags 0x07，100 项 TOC | VBR（`vbr_quality=2`） | `map=toc`；同上 |
| 4 | `seek-4-vbr-nohdr.mp3` | 850421 | 无 | 同上 VBR | `map=lin`；静音处比特率低，线性映射偏差较大，只记录 |

- VBR 两首共用同一段音频码流（静音段比特率低，哔声处高），所以字节数远小于 CBR；3 号 = 头帧 417 B + 4 号全部字节。
- 字节数以 `lameenc` 1.9.0 为准；同参数两次生成逐字节一致。合同由 `python Tools/board_test_card/test_card_xing.py`（头字段、TOC 手算字面量、帧数与独立遍历一致，不需要 `lameenc`）与 `python Tools/board_test_card/test_card_seek.py`（哔声位置、头部类型、不进 `all`、提交夹具规格）检查；这些测试不在 `verify.ps1` FULL 内，需手动运行，其中整文件用例要编码 3:30 音频，约数秒至数十秒，未装 `lameenc` 时跳过。
- 另有主机精度夹具 `--set host-fixtures`（20 s、约 160 KB，不上板、不复制进卡），输出目录指向 `Tests/playback_service/fixtures/xing`，规格与生成命令见[夹具 README](../../Tests/playback_service/fixtures/README.md#xing跳转精度夹具174)。
- ffmpeg 的 CBR+Info 对照样本只写生成方法（见同一夹具 README），不提交，不进测试。
