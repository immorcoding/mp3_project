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
