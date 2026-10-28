# 中文字体资源

曲目元数据（曲名、歌手）使用的字体（中文与西文），作为资源包 v4 的 FONT 条目（ResourceID 5）下发。本目录只存放可复现的生成输入规则和生成结果；打包器直接读取已入库的 bin，日常打包与 CI 不依赖 Node 或 OTF（RES-7）。

| 文件 | 内容 |
| --- | --- |
| `noto_sans_cjk_sc_12_4bpp.bin` | 生成的字体 bin，623520 B，SHA-256 `5c6f3f8383ef376f19e9fde9ab7c0e9b983de07f10a1a74c0e4694a3e5e17bbb`，CRC32 `0x847F08C2` |
| `generate_font.py` | 生成脚本，同时是字表规则的唯一来源 |
| `OFL.txt` | 字体许可证（SIL Open Font License 1.1），取自 `notofonts/noto-cjk` 的 `Sans2.004` 标签 |

## 生成输入

- 字体：Noto Sans CJK SC Regular 2.004 OTF，SHA-256 `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`；来源 `notofonts/noto-cjk` 的 `Sans2.004` 标签下 `Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`。字体版权声明为 © 2014-2021 Adobe。OTF 约 16 MB，不入库；脚本校验 SHA-256，不符即失败。
- 转换器：`lv_font_conv` 1.5.3，参数 `--size 12 --bpp 4 --format bin --no-compress`。脚本用 `npm install --prefix <临时目录> lv_font_conv@1.5.3` 取得该版本后直接用 `node` 调用，而不是 `npx`：Windows 的 `npx.cmd` 经 `cmd.exe` 转发，命令行上限 8191 字符且按系统代码页传参，装不下 7000 余个汉字。
- 字集（共 7619 个字符，加索引 0 的 notdef 共 7620 个字形条目；Pack v4 起在 v3 的 7540 个字符之上净增 79 个）：
  - ASCII `0x20`–`0x7E`，95 个，同样取自该字体，不挂 Montserrat 作 fallback；
  - Latin-1 补充区 U+00A0–U+00FF，96 个（含不换行空格与软连字符）；
  - U+2013（–）与 U+2122（™）；
  - GB2312 第 1–9 区全部 682 个符号（含全角标点、日文假名、希腊与西里尔字母）；
  - GB2312 第 16–87 区全部 6763 个汉字。

  各部分取并集去重：GB2312 符号区已含 19 个 Latin-1 字符（`¤ § ¨ ° ± × à á è é ê ì í ò ó ÷ ù ú ü`），所以 98 个新增候选（Latin-1 96 个加两个标点）净增 79 个。GB2312 第 1 区本来就含 `— ‘ ’ “ ” …`。符号与汉字由 CPython 的 `gb2312` 编解码器按区位顺序解码 `0xA1A1`–`0xF7FE` 得到，脚本检查数量，繁体和 GBK 扩展不在本版本范围内。字集规则只在 `generate_font.py` 的 `charset_codepoints()` 一处，改字集时同步本节数字。

## 重新生成与复现检查

```powershell
python Resources/fonts/generate_font.py --otf <NotoSansCJKsc-Regular.otf>          # 覆盖生成
python Resources/fonts/generate_font.py --otf <NotoSansCJKsc-Regular.otf> --check  # 只比对，不覆盖
```

需要 Python 3.10+、Node.js 与 npm。同一输入两次生成的字节相同（已核对：两次输出 SHA-256 相同，`--check` 通过）。更换字体、字号或字集时重新生成，并递增 `Tools/package_maker/resource_pack.json` 中该条目的 `version`（v4 起为 2）与 `package_version`（v4 起为 4）。

## 字节契约与限制

- 条目 Data 就是这个 bin 的原样字节；FONT V1 Metadata 固定 32 B，其中字形数、字号 12、Ascent 13、Descent −3 由打包器从 bin 的 head 与 loca 读出，见 `Tools/package_maker/lv_font_bin.py`。当前 bin 的位图总量为 520360 B，kern 字距对 7252 对，cmap 56 个子表。
- 位图总量必须小于 1 MiB：`lv_conf.h` 中 `LV_FONT_FMT_TXT_LARGE = 0`，字形位图索引只有 20 位。打包器与 `ResourcePack_DecodeFontMetadata()` 都会拒绝达到上限的字体。扩充字集超出时，须另行决定是否修改 `lv_conf.h`。
- 位图在 bin 中不保证字节对齐，字体消费者启动时需解析一次并重排；字体不能被 `lv_font_load` 直接使用（无文件系统驱动，且会整段占用 LVGL 堆）。
