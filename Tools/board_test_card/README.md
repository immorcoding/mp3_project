# 板级测试卡

[make_card.py](make_card.py) 合成一组 MP3，供 [板级验收清单模板](../../docs/agents/board-test-checklist.md) 使用。内容全部是脚本计算的确定性和弦，不含第三方录音；生成物不进 Git。

```powershell
pip install lameenc
python Tools/board_test_card/make_card.py <输出目录>
python Tools/board_test_card/make_card.py <输出目录> --copy-to I:\Music --set all
```

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
| 7–9 | 长 ASCII、纯中文、中英混排各一 | 去掉 `.mp3` | 12 px 下超过 160 px，需滚动 |

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
- 样本 4：v2.4，TPE1 共 333 B（上限 251 B）：前 249 B 为 ASCII，其后是 3 B 汉字，截断点落在第一个汉字内部，所以按字符边界只留 249 B，不加省略号、不留半个字符。249 个字符远宽于 Queue 与唱盘页的可见宽度，Queue 歌手行用 `LV_LABEL_LONG_DOT`。唱盘页只显示曲名，歌手不上唱盘页。
- 字节数以 `lameenc` 1.9.0 为准；同参数两次生成逐字节一致。合同由 `python Tools/board_test_card/test_card_glyphs.py` 检查（标签版本、编码字节、关键码点、截断边界、不进 `all`；不需要 `lameenc`）。该测试不在 `verify.ps1` FULL 内，需手动运行。
