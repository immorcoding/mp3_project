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

- 固件按 FAT 目录项顺序入库，目录顺序等于创建顺序：必须按上表顺序逐个复制，不要整夹拖拽；`--copy-to` 已按此顺序。
- `--set first-bad`（坏文件在首位）、`--set all-unsupported`（整库不可播，期望 ALL_BAD）复制子集，相对顺序不变；换子集前先格式化卡。
- 每个和弦 1 s、起止采样为 0，循环拼接，曲内没有人为咔哒声；同一 `lameenc` 版本下输出逐字节一致，上表字节数以 `lameenc` 1.9.0 为准。
