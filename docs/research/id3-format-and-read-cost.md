# ID3 格式要点与板上读取代价（#129 调研）

- 票：immorcoding/mp3_project #129「ID3 格式要点与板上读取代价的事实」（OPEN，https://github.com/immorcoding/mp3_project/issues/129）
- 日期：2026-10-27
- 对象：ID3v1/v1.1、ID3v2.2/2.3/2.4 的结构与帧规则；主流写标签工具与读取端的实现行为；仓库内读取代价记录；FatFs `cc936.c` 的 `ff_convert`。
- 用途：为后续 ID3 相关 spec 提供事实依据。本文只记录事实、未决项与测量项，不含设计方案。

证据标记：
- 【确认】：规范原文、厂商官方页或本仓库源码/配置可直接读到（给出 URL 与章节，或路径与行号）。
- 【佐证】：第三方实现的源码或注释（mutagen、TagLib）或第三方文档，与规范互证，不是规范原文。
- 【二手】：论坛、邮件列表、博客、搜索摘要。只作线索。
- 【推断】：由上述证据推导，未经原文或实测；写出推导式。
- 【未核对】：需要的原文无法访问或未逐字核对；写明尝试方式。
- 【需板上测量】：仓库内没有实测记录；不以推断填补。
- 【未找到】：检索后无对应记录。

引文规则：英文引文仅取短句，括号内给中文译意；`should`、`MUST` 等规范用词保留原文。访问日期均为 2026-10-27。

## 0. 结论摘要

1. 标签头 10 字节；大小字段 4 字节，每字节最高位为 0（28 位）。v2.3 大小不含头、含扩展头与填充；v2.4 大小为 syncsafe，有 footer 时为总长 −20，否则 −10。【确认】id3.org/id3v2.3.0 §3.1；id3.org/id3v2.4.0-structure §3.1
2. 帧头：v2.3 为 4 字符 ID + 4 字节普通大小 + 2 字节标志，大小为帧长 −10；v2.4 大小为 syncsafe；v2.2 为 3 字符 ID + 3 字节大小（不含 6 字节头）。【确认】（v2.3、v2.4）；【佐证】（v2.2）
3. 文本编码：v2.3 定义 `$00`（ISO-8859-1）与 `$01`（UCS-2，必须以 BOM 开头）；v2.4 增加 `$02`（UTF-16BE 无 BOM）与 `$03`（UTF-8）。两版均未列 GBK。【确认】
4. 多值：v2.4 文本帧正文可含以 null 分隔的多个字符串；v2.3 原文未找到相应条款。除 TXXX 外，文本帧每标签只能出现一次。【确认】
5. 帧顺序：v2.3 与 v2.4 均写 "There is no fixed order"（无固定顺序），TIT2、TPE1 的字节位置不固定。【确认】
6. 去同步：v2.3 只有标签级（头 bit 7）；v2.4 为帧级（帧标志 n），并保留标签级标志。【确认】
7. ID3v1：文件末 128 字节，以 "TAG" 开头；原文未规定字符集。v1.1 注释 28 字节，第 125 字节为 0，第 126 字节为曲目号。【确认】
8. 写端：Mp3tag 官方页列出 ID3v2.4 UTF-8、ID3v2.3 UTF-16、ID3v2.3 ISO-8859-1 等选项，未写默认值。【确认】foobar2000 的默认与兼容行为、iTunes 当前行为未找到一手来源（仅【二手】线索）；国内音乐客户端未找到一手来源。
9. 读取端：mutagen 与 TagLib 对非 syncsafe 的 v2.4 帧大小都有回退（mutagen 按帧数择优，TagLib 按下一帧 ID 位置校验）。【佐证】
10. 仓库现状：`APP/tasks/storage/stream/storage_audio_tag.c` 只越过 ID3v2 头与尾部 ID3v1/APEv2；仓库内无 TIT2/TPE1 解析代码。【确认】
11. 读取代价：仓库内无 SD 顺序读吞吐、`f_open` 耗时、欠载次数的实测记录；SD 与 SDRAM 基准开关默认关闭。【需板上测量】
12. FatFs：`WCHAR ff_convert(WCHAR chr, UINT dir)`，dir 0 为 Unicode→OEM、1 为 OEM→Unicode；双字节码首字节位于高 8 位；未映射返回 0。两表与 Microsoft 的 CP936.TXT 全部 21791 个双字节映射双向一致，不含用户区码位。【确认】
13. 位置：两张 87172 B 表的链接地址在 SDRAM NOLOAD 段 `.sdram_external_resources`（0xC0000000 起），由 Resource Service 从资源包复制；`ff_convert`、`ff_wtoupper` 代码及 `cvt` 表在内部 Flash。【确认】map 早于链接脚本提交 932a685 为【推断】（见第 1 节）。
14. 版本：本地 FatFs 为 R0.12c（`ff.h:2`；`_FATFS 68300` 于 `ff.h:22`）；官方首页当前标注 R0.16。【确认】

## 1. 范围与来源

- 研究问题取自 issue #129 正文（`gh issue view 129 --json body`，评论数 0）。
- 仓库：worktree `E:\Projects\project_mp3\source_code\.claude\worktrees\research-id3`，分支 `research/id3-format-and-read-cost`，HEAD `932d6db`（2026-10-26T21:47:45-07:00）。`docs/research/` 原不存在，本票新建。
- map：主检出 `build/firmware-debug/mp3_project.map`（只读），文件修改时间 2026-10-22 11:24（本机时间，非构建日志时间戳）。该时间之后 `Middlewares/Third_Party/FatFs` 与 `FATFS/` 在 git 历史中无提交（`git log --since='2026-10-22 11:24'` 无输出；最后一次触及 FatFs 路径的提交为 `d15ed4f`，2026-08-09 19:31 +08:00）。【确认】
- `STM32H743xG_flash.ld` 有提交 `932a685`（2026-10-26T11:57:30-07:00，新增两个 SDRAM NOLOAD 区）。map 中无 `font_resource` 符号（计数 0），据此推断 map 早于该提交。【推断】map 与 HEAD 的对应关系未证明（N10）。
- 固定版本：mutagen `release-1.48.1` = `452190d42ab2092577c77342d49157f8768d796f`；TagLib `v2.3.2` = `deadc2990767dfbda0701e0ab35fdeea653db08f`（`gh api repos/{owner}/{repo}/tags`，2026-10-27）。
- 下载文件保存于 scratch 的 `id3-research\` 下，每个文件独立目录；未运行其中任何代码。

## 2. 第一部分：ID3 格式要点（一手：id3.org）

### 2.1 标签头与扩展头
- 头布局：`ID3`、版本 2 字节、标志 1 字节、大小 4 字节（v2.3 原文："currently only three flags are used"，即目前只用三个标志位）。【确认】id3.org/id3v2.3.0 §3.1
- v2.3 标志：a（bit 7）为去同步，"a set bit indicates usage"（置位即表示使用）；c（bit 5）为 experimental。b（bit 6，扩展头）未逐字核对。【未核对】（N6）
- v2.4 标志 `%abcd0000`：a 去同步、b 扩展头、c 实验、d（bit 4）footer；其余位 MUST 清零（必须为零）。【确认】id3.org/id3v2.4.0-structure §3.1
- 大小：v2.3 为 `4 * %0xxxxxxx`，"Only 28 bits … are used"（只用 28 位），为去同步后的整标签长度，含填充与扩展头，不含 10 字节头。【确认】v2.3 §3.1。v2.4 为 32 位 syncsafe，覆盖扩展头、填充与帧；有 footer 时为 `'total size' - 20`，否则 `- 10`。【确认】v2.4 structure §3.1
- footer（仅 v2.4）：10 字节，为头的副本，标识 "3DI"；附于音频之后的标签 "It is REQUIRED to add a footer"（须加 footer）。【确认】v2.4 structure §3.4
- 扩展头 v2.3：大小字段 4 字节，"excludes itself"（不含自身）；扩展标志 2 字节；填充大小 4 字节；x 位为 CRC-32。【确认】v2.3 §3.2。原文未写 v2.3 扩展头大小为 syncsafe。【确认（无条款）】
- 扩展头 v2.4：大小为 32 位 syncsafe，"never have a size of fewer than six bytes"（不少于 6 字节）；CRC 为 35 位 syncsafe。【确认】v2.4 structure §3.2。扩展头各标志位细节【未核对】（N6）。
- 填充（v2.4）："The value of the padding bytes must be $00. A tag MUST NOT have any padding between the frames or between the tag header and the frames."（填充字节值须为 $00；标签头与帧之间、帧与帧之间不得有填充）；有 footer 时 MUST NOT 有填充。【确认】v2.4 structure §3.3

### 2.2 帧头与帧 ID
- v2.2：3 字符 ID + 3 字节大小，无帧标志；大小为 "frame size - 6"（帧长减 6）。【佐证】mutagen-specs v2.2 页（https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html，WebFetch 成功）；mutagen `_tags.py:613–627` 的 v2.2 分支读取 `>3s3s` 并取 `data[6:6+size]`。【佐证】id3.org/id3v2.2.0 返回 404（失败）。
- v2.3：4 字符 ID + 4 字节普通大小 + 2 字节标志；"The size is calculated as frame size excluding frame header (frame size - 10)"（大小为帧长减去帧头 10 字节）。【确认】v2.3 §3.3
- v2.4：4 字符 ID + 4 字节 syncsafe 大小（"stored as a 32 bit synchsafe integer"，以 32 位 syncsafe 整数存储）+ 2 字节标志。【确认】v2.4 structure §4
- 帧 ID：A–Z、0–9；X、Y、Z 开头为实验用。【确认】v2.3 §3.3、v2.4 §4

### 2.3 帧标志
- v2.3 status：a Tag alter preservation、b File alter preservation、c Read only；format：i Compression、j Encryption、k Grouping identity。【确认】v2.3 §3.3.1
- v2.3 附加字段按标志顺序位于帧头后："the four bytes of decompressed size will precede the encryption method byte"（解压后大小 4 字节位于加密方法字节之前）；压缩为 zlib，加密方法 1 字节，分组标识 1 字节。【确认】v2.3 §3.3.1
- v2.4 status a/b/c；format h Grouping、k Compression、m Encryption、n Unsynchronisation、p Data length indicator。【确认】v2.4 structure §4.1。TagLib 位映射：status 为 bit 6/5/4，format 中 h=bit 6、k=bit 3、m=bit 2。【佐证】`id3v2frame.cpp:578–589`
- k：压缩时 "A 'Data Length Indicator' byte MUST be included in the frame"（压缩帧必须包含 Data Length Indicator 字节）；m：是否需要 Data Length Indicator "depends on the specific algorithm used"（取决于具体算法）；n：去同步 "is not made mandatory"（不强制要求）该字节；p：指示值为 32 位 syncsafe。【确认】v2.4 structure §4.1.2
- 未知标志（v2.4）："the frame MUST NOT be changed without that bit cleared"（须先清除该位，否则不得改动帧）；第二字节未知则 "likely to not be readable"（可能不可读）。【确认】v2.4 structure §4.1

### 2.4 去同步
- 规则：`%11111111 111xxxxx` → `%11111111 00000000 111xxxxx`；所有 `$FF 00` 替换为 `$FF 00 00`。【确认】v2.4 §6.1、v2.3 §5
- 作用范围：v2.3 只有标签级（"To indicate usage … the first bit in 'ID3 flags' should be set"，即应置位标签标志第一位），v2.3 帧标志中无去同步位。【确认】v2.3 §5。v2.4 改为帧级："Unsynchronisation is done on frame level, instead of on tag level"（去同步在帧级进行，而非标签级）。【确认】v2.4 changes §3
- 标签级标志：全部帧去同步时 SHOULD 设置；有未去同步的帧时 "MUST NOT be set"（不得设置）。帧级标志：帧被改动时 MUST 设置。【确认】v2.4 §6.1
- 大小：v2.3 标签大小按去同步后计（含插入的 `$00`）。【确认】v2.3 §3.1。v2.4 帧大小为 "after encryption, compression and unsynchronisation"（加密、压缩与去同步之后）的数据长度。【确认】v2.4 §4
- 解码顺序（v2.3）："the 'unsynchronisation scheme' should be parsed first, decompression afterwards"（应先解去同步，再解压）；末字节为 `$FF` 且需消除误同步时，原文写 "at least one byte of padding should be added"（应补至少一字节填充）。【确认】v2.3 §5

### 2.5 文本编码字节 0–3
- v2.3 默认："a string is represented as ISO-8859-1 characters in the range $20 - $FF"（字符串默认为 ISO-8859-1，范围 $20–$FF）。【确认】v2.3 §3.3
- v2.3 `$00`：ISO-8859-1，终止 `$00`。`$01`："All Unicode strings use 16-bit unicode 2.0 (ISO/IEC 10646-1:1993, UCS-2). Unicode strings must begin with the Unicode BOM ($FF FE or $FE FF)"（Unicode 串采用 UCS-2；必须以 BOM `$FF FE` 或 `$FE FF` 开头），终止 `$00 00`。v2.3 无 `$02`、`$03` 条款。【确认】v2.3 §3.3
- v2.4 `$00` ISO-8859-1（终止 `$00`）；`$01` UTF-16 with BOM，"All strings in the same frame SHALL have the same byteorder"（同一帧内字节序须一致），终止 `$00 00`；`$02` UTF-16BE without BOM（终止 `$00 00`）；`$03` UTF-8（终止 `$00`）。【确认】v2.4 structure §4
- 空串 BOM：两版均允许 NULL 终止的空 Unicode 串带 BOM，如 `$FF FE 00 00`、`$FE FF 00 00`。【确认】
- 两版编码表均不含 GBK。v2.3 原文中 `$01` 必须带 BOM，未允许无 BOM 的 `$01`。【确认（条款与无条款）】

### 2.6 多值、TIT2、TPE1、帧唯一性
- v2.4："All text information frames supports multiple strings, stored as a null separated list"（文本帧支持多个字符串，以 null 分隔成列表）。【确认】v2.4 §4.2。v2.3：原文未找到多值分隔条款（检索 null separated、separated、multiple 无条目）。【确认（无条款）】
- TIT2：v2.3 §4.2.1 "Title/songname/content description"（标题）；v2.4 §4.2.1 "is the actual name of the piece"（即作品的实际名称）。【确认】TPE1：v2.3 §4.2.1 "Lead performer(s)/Soloist(s)"（主要演奏者）；v2.4 §4.2.2 "is used for the main artist"（用于主要艺术家）。【确认】
- 唯一性：文本帧 "There may only be one text information frame of its kind in an tag"（每标签同类文本帧只能有一个）（v2.3 §4.2、v2.4 §4.2）。【确认】TXXX："more than one "TXXX" frame in each tag, but only one with the same description"（每标签可有多个 TXXX，但描述须唯一）（v2.4 §4.2.6）。【确认】URL 帧同理（除非帧说明另有规定）。【确认】

### 2.7 帧顺序与帧长
- v2.3 §3.3："There is no fixed order of the frames' appearance in the tag"（帧在标签中的出现顺序无固定要求）；v2.4 §4 同句，示例 "UFID, TIT2, MCDI, TRCK"。【确认】
- 帧最小长度："A frame must be at least 1 byte big, excluding the header"（不含帧头，帧至少 1 字节）。【确认】v2.4 §4
- 因无固定顺序，TIT2、TPE1 的偏移需由前序帧大小推得（见 4.7，N12）。【推断】

### 2.8 ID3v1 / v1.1
- 位置："Seek end of file, then -128, if first 3 bytes='TAG' then it has info layed"（定位到文件末尾后退 128 字节；若前 3 字节为 TAG 则含标签信息）（https://id3.org/ID3v1，成功）。【确认】
- 偏移：`0..2` TAG（3 字节）、`3..32` 标题（30）、`33..62` 歌手（30）、`63..92` 专辑（30）、`93..96` 年份（4）、`97..126` 注释（30）、`127` 流派（1）。【确认】
- v1.1："should be 28 characters"（注释应为 28 字符）、字节 125 "should be zero"（原文应为 0）、字节 126 为 "which track on the CD"（CD 曲目号）。【确认】
- 流派：字节 127 查表；附录 A（v2.3 §8.1）列 0 "Blues" 起的 80 项，80 "Folk" 起为 Winamp 扩展。【确认】
- 填充："all non-filled fields must be padded with zeroed bytes"（未填满的字段须以 0 字节填充）。【确认】字符集：页面未规定（检索 ISO、latin、charset、encoding 无命中）。【确认（无条款）】

### 2.9 未知帧与非法值
- 未知帧：仅帧标志 a/b 规定 preserved（保留）/ discarded（丢弃）。【确认】v2.3 §3.3.1、v2.4 §4.1.1
- 非法大小、非法编码字节：v2.3、v2.4 原文检索 invalid、illegal 无命中，未找到条款。【确认（无条款）】（N7）

## 3. 第一部分 B：写标签工具与读取端容错

来源为 mutagen（`mutagen/id3/_util.py`、`_specs.py`、`_tags.py`、`_frames.py`、`_file.py`）与 TagLib（`taglib/mpeg/id3v2/*.cpp`、`taglib/mpeg/id3v1/id3v1tag.cpp`），固定到第 1 节的 commit；本地副本位于 scratch。

### 3.1 写端（实现者源码与厂商页）
- mutagen 默认写 v2.4：`ID3SaveConfig(v2_version=4, v23_separator=None)`（`_util.py:19–24`）；无标签时默认版本为 (2,4,0)（`_file.py:74–76`）。【佐证】
- mutagen 写 v2.3：编码非 0/1 时改为 UTF-16（`_specs.py:266–269`）。多值文本：`v23_separator` 为空则保留为多个字符串，否则以分隔符连接成单字符串（`_specs.py:534–551`）。【佐证】
- TagLib 写 v2.3：UTF-8、UTF-16BE 改为 UTF-16；Latin-1 字段含非 Latin-1 字符时，v2.4 写 UTF-8、v2.3 写 UTF-16（`id3v2frame.cpp:367–387`）。【佐证】v2.3 不支持的帧列表注释 "iTunes writes and reads TSOA, TSOT, TSOP to ID3v2.3"（iTunes 读写 TSOA、TSOT、TSOP 时按 v2.3 处理）（`id3v2tag.cpp:558–567`）。【佐证】
- TagLib ID3v1 渲染：非 Latin-1 字段输出空字节（`id3v1tag.cpp:68–80`）。【佐证】
- Mp3tag 官方页（https://docs.mp3tag.de/customization/options/tags/mpeg/，curl 200；存档快照 200）。【确认（厂商页）】选项原文："ID3v2.4 UTF-8 — Tags are written in Unicode as UTF-8 encoded string"（标签以 UTF-8 编码的 Unicode 写入）；"ID3v2.3 UTF-16 — writes ID3v2.3 tags in Unicode encoded as UTF-16 LE"（以 UTF-16 LE 写 v2.3）；"ID3v2.3 ISO-8859-1 — doesn’t use Unicode when writing ID3v2 tags"（写 v2.3 时不用 Unicode）；另有 ID3v1、"ID3v2 only if ID3v1 too small"（仅当 ID3v1 不够用时写 ID3v2）等选项。页面未写默认值（检索 default 无命中）。【确认（无默认值）】WebFetch 对该域名被工具安全检查拒绝，改用 curl 取得同一页。
- foobar2000：官方更新日志（https://www.foobar2000.org/changelog，WebFetch 成功）仅含 1.5 之后条目，未见 ID3v2 写入版本或编码条目。【确认（该页检索结果）】论坛摘要（hydrogenaudio，2003–2011）称默认 v2.4/UTF-8，兼容选项写 v2.3/UTF-16 且不写填充与去同步。【二手】未找到一手来源（N1、N2）。
- iTunes：TagLib 注释认为 iTunes 写 v2.4 帧大小为 v2.3 式普通整数（`id3v2frame.cpp:566–567`）。【佐证】2005–2007 年 ID3 开发者列表称 iTunes 的转换保留原大小字节。【二手】当前版本行为未核对（N3）。
- 国内音乐客户端（网易云音乐、QQ 音乐）：未找到一手来源；搜索仅得到通用工具与文章。【二手】需样本核对（N4）。

### 3.2 读取端容错（只记录实现行为）
- mutagen 头：标签大小非 syncsafe 报错 "Header size not synchsafe"（头大小非 syncsafe）（`_tags.py:72–73`）；头标志非法报错（`:75–80`）；扩展头大小非 syncsafe 报错（`:103–105`）。【佐证】
- mutagen v2.4 帧大小：`determine_bpi` 分别按 syncsafe 与普通整数遍历，统计能识别的帧 ID 数，取多者（`_tags.py:117–165`；docstring "Needed because iTunes used to write normal ints for frame sizes."，即需要此判定，因为 iTunes 过去写普通整数帧大小）。v2.3 固定普通整数（`:570–572`）。【佐证】
- mutagen 其它：整标签去同步失败则忽略（`_tags.py:564–568`）；帧级去同步失败则按原样读取，注释引用 issues 210、223（`_frames.py:252–260`）；v2.2 帧名出现在 v2.3 标签中时映射（`_tags.py:597–600`）；0 字节帧丢弃，全零 ID 视为填充并停止（`:582–589`）；合法但未知的帧 ID 保留为未支持帧（`:604–605`）。【佐证】
- mutagen 去同步解码：`FF` 后为 ≥ `E0`、`00` 或末尾时报 "invalid sync-safe string"（非法 syncsafe 字符串），`00` 被删除（`_util.py:47–61`）。UTF-16 无 BOM 按小端解码（`_specs.py:435–448`，注释 "utf-16 is missing BOM, content is usually utf-16-le"，即 UTF-16 缺 BOM，内容通常为 UTF-16LE）。【佐证】
- TagLib v2.4 帧大小：size > 127 时，若按 syncsafe 得到的下一帧 ID 位置非法而按普通整数位置合法，则采用普通整数（`id3v2frame.cpp:566–576`，注释 "iTunes writes v2.4 tags with v2.3-like frame sizes"，即 iTunes 写 v2.4 时使用类似 v2.3 的帧大小）。【佐证】
- TagLib 同步整数：字节最高位为 1 时注释 "assume this was created by some buggy software"（假定由有缺陷的软件写出），改按普通大端 32 位整数（`id3v2synchdata.cpp:31–58`）。【佐证】
- TagLib 其它：重复 ID3v2 标签的 workaround，注释称 TagLib 旧版本可能写出重复标签（`id3v2tag.cpp:804–808`）；帧数上限常量 `MAX_ID3V2_FRAME_COUNT = 50000`（`id3v2tag.cpp:59`）；ID3v1 字段按 Latin-1 解码，v1.1 判定为 comment[28]==0 且 track≠0（`id3v1tag.cpp:68–80`、`:226–262`）。【佐证】

### 3.3 常见写法与不合规写法（来源与可信度）
- v2.3 中使用 UTF-8：v2.3 无编码 `$03`【确认】；mutagen 与 TagLib 写 v2.3 时均改为 UTF-16（见 3.1）【佐证】；读取端对帧内编码字节按其声明解码（`id3v2frame.cpp:342–365`）【佐证】。
- v2.4 大小用普通整数（iTunes 类）：两者均有回退（见 3.2）。【佐证】
- ID3v1 中写 GBK 字节：TagLib 按 Latin-1 读取、非 Latin-1 渲染为空。【佐证】一个基于 mutagen 的工具项目称其在 ID3v1 中写入 GBK 副本（https://code.google.com/p/mp3tagiconv，搜索摘要）。【二手】
- GBK 字节写入编码 0：规范编码 0 为 ISO-8859-1。【确认】FFmpeg ticket #3904 描述 Windows 程序把 GB 文本标为 ISO-8859-1（https://trac.ffmpeg.org/ticket/3904，搜索摘要）。【二手】Ubuntu 中文论坛称 "GBK is not a real ID3 encoding"（GBK 不是真正的 ID3 编码）（https://forum.ubuntu.org.cn/viewtopic.php?p=726208，搜索摘要）。【二手】

## 4. 第二部分：板上读取代价（仓库内可核对的记录）

### 4.1 SD 顺序读吞吐
- 仓库内未找到实测记录。【需板上测量】（M1）`gh search issues` 的命中未逐条核对；本地保存的 #81、#87、#110、#122 正文检索 "MB/s"、"KB/s" 无命中。【未找到】
- 代码：`APP/tasks/storage/benchmark/storage_sd_benchmark.c`（经 Service 文件接口的 64 MiB 读写，注释 `:8–10`；计算 "百分之一 MiB/s" 见 `:119`）。【确认】开关 `APP/app_config.h:22` `STORAGE_SD_BENCHMARK_ENABLE 0`（产品默认关闭）。【确认】

### 4.2 f_open 耗时与「打开并读前 N KB」
- 未找到实测记录。【需板上测量】（M2、M3）
- 结构上界：`APP/tasks/storage/README.md:48`："开启 ≤ f_open + 标签探测（读头、读尾、seek）+ 一次块读取"（结构描述，非实测）。【确认】同义注释见 `APP/tasks/storage/stream/storage_audio_stream.c:397`。
- 头尾读取量：`APP/tasks/storage/stream/storage_audio_tag.h:13` 头 10 字节，`:14` 尾 160 字节。【确认】

### 4.3 storage_sdram_benchmark
- 未找到输出记录（命令、输出、固件版本均无）；`gh search issues "storage_sdram_benchmark"` 仅命中本票 #129。【未找到】
- 开关 `APP/app_config.h:26` `STORAGE_SDRAM_BENCHMARK_ENABLE 0`（产品默认关闭）；调用点 `APP/tasks/storage/storage_task.c:253–254`；`APP/tasks/storage/benchmark/README.md:8` 注明 `storage_task.c` 当前未包含该头。【确认】

### 4.4 音频流块配置（32 KiB × 32）
- 单块：`APP/tasks/storage/stream/storage_audio_config.h:10` `STORAGE_AUDIO_BLOCK_BYTES (32U * 1024U)`，注释 "不超过 SD bounce buffer（32 KiB）"。【确认】
- 块数：`storage_audio_config.h:11` `STORAGE_AUDIO_BLOCK_COUNT 32U`，注释 "初值 32 块共 1 MiB，320 kbps 下约 26 s"。【确认】
- 块池：`APP/tasks/storage/stream/storage_audio_block.c:13`（static_assert ≥ 3）、`:16`、`:20–21`（静态数组）、`:36–45`（队列创建与入队）。【确认】
- 总缓冲 32 × 32 KiB = 1 MiB。【推断】（算术；见 N12）
- 与 SD 读并发时的断流余量：未记录（`APP/tasks/storage/README.md:57` 称 "归板级验收票"）。【需板上测量】（M7）
- SD 超时口径：`docs/shape/storage.md:16`（`FILESYSTEM_FATFS_BSP_DMA_TIMEOUT_MS` 500 ms，按每个 32 KiB DMA 分块）。【确认】

### 4.5 欠载（underrun）
- 仓库内未找到欠载次数的实测记录。【未找到】
- 计数：`Platform/audio/platform_audio.c:157`（释放半区时另一半区未提交则 `Underruns++`）。【确认】上层：`Service/playback/playback_service.c:839–842`、`APP/tasks/playback/playback_machine.c:783–784`。【确认】
- 无日志出口：#81 验收 C1 记录 "Underruns 无日志出口，仅听感无断续"（Underruns 无日志输出，仅听感无断续），观察约 12.8 min，为 44.1 kHz 单格（评论 https://github.com/immorcoding/mp3_project/issues/81#issuecomment-6274815372，2026-10-19T18:53:53Z；同文亦见 …#issuecomment-6274816505）。【确认】
- 目标值（非实测）：#62 "稳态欠载次数 underruns 0；32、44.1、48 kHz 各连续播放 ≥ 2 h"（目标：稳态欠载 0，三种采样率各连续播放 2 小时以上）（https://github.com/immorcoding/mp3_project/issues/62）。【确认（目标）】
- SD 读并发条件下的欠载：未找到。【需板上测量】（M7）

### 4.6 唱盘页与 Queue 读取时延
- 未找到读取时延实测。【未找到】（M10）#122 T3 为定性记录（切歌后旧名短暂保留）。【确认】
- 仅有的时间实测不针对 SD：#122 F3 "FONT 复制 + CRC：265.0 / 264.7 / 264.8 ms；解码：488.2 / 488.5 / 488.2 ms（glyphs=7541, bytes=618952）；Resource 初始化 196 ms → 461 ms"（评论 https://github.com/immorcoding/mp3_project/issues/122#issuecomment-6432651748，2026-10-27T03:00:09Z；固件 `v0.6.1-30-g6b37811 Debug`；卡 30436 MB FAT32）。【确认】复制段 `.sdram_font_resource` 为 0x94800 B（同评论 F4）。【确认】由此约 2.3 MB/s（608256 B ÷ 0.265 s，仅复制与 CRC）。【推断】（N17）

### 4.7 读取 TIT2 / TPE1 所需字节范围
- 头 10 字节（v2.3、v2.4）；v2.2 帧头 6 字节。【确认】/【佐证】
- 标签总长：v2.3 = 10 + 大小字段；v2.4 = 10 + 大小字段（有 footer 时另加 10）。【确认】
- 第 k 帧起始偏移 `o_k = 10 + 扩展头长度 + Σ_{i<k}(帧头长 + 帧大小_i)`，帧头长 v2.3/v2.4 为 10、v2.2 为 6。【推断】（N12）
- TIT2 所需范围 `[o_TIT2, o_TIT2 + 10 + size_TIT2)`，TPE1 同理；位置未知时上界为 `[0, 标签总长)`。【推断】（N12）
- v2.4 非 syncsafe 大小的写法需按 3.2 的回退规则判定帧长，否则偏移不可靠。【佐证】（N13）
- ID3v1：规范为 "Seek end of file, then -128"（文件末尾后退 128 字节）（§2.8）。【确认】仓库现行读取为末尾 160 字节（4.2）；定位与读取代价未测。【需板上测量】（M5）

### 4.8 标签跳过与 ID3 既有记录（仓库内）
- 越过标签的现行实现：`APP/tasks/storage/stream/storage_audio_tag.c:37–65`（ID3v2 头：魔数 "ID3"；大小字节 6–9 须为 syncsafe；标志 `0x10` 时加 10 字节 footer）与 `:68–122`（尾部 ID3v1 与 APEv2 脚）。【确认】
- 主机测试：`Tests/storage_audio_stream/test_storage_audio_tag.c:107–167`（扩展头、footer、synchsafe、非法字节、越界等）。【确认】
- 文档：`docs/adr/0017-mp3-decoder-minimp3.md:23`（文件层跳过 ID3v2 等非音频头）；`GLOSSARY.md:90`；`docs/shape/storage.md:15`（"播放时越过 ID3v2 与尾部 ID3v1/APE 归 Storage"）。【确认】
- 未找到 TIT2/TPE1 解析代码或定义（检索 APP、Service、Platform、Tests、docs、GLOSSARY.md、minimp3）。【未找到】
- 1 MB ID3 标签文件跳过耗时：板测清单 A9 未填（`docs/agents/board-test-checklist.md:55`）。【需板上测量】（M6）
- 开放议题（仅指向，不采纳其中任何方案）：#127（0.8.0 ID3 标题与歌手）、#128（中文曲库 ID3 标签实测，正文为供用户运行的统计脚本，未见实测数值）、#131（ID3 文本编码策略）、#132（ID3 解析时机、读取范围与缓存）；均为 OPEN。【确认】

## 5. 第三部分：FatFs cc936.c 与 ff_convert

### 5.1 接口与语义
- 签名 `WCHAR ff_convert (WCHAR chr, UINT dir)`（原型 `Middlewares/Third_Party/FatFs/src/ff.h:301`；定义 `Middlewares/Third_Party/FatFs/src/option/cc936.c:10923–10956`）。【确认】
- dir："0: Unicode to OEM code, 1: OEM code to Unicode"（0：Unicode 到 OEM 码；1：OEM 码到 Unicode）（`cc936.c:10925`）。【确认】
- 返回值：转换结果；注释 "0 means conversion error"（0 表示转换错误）（`:10923`），实现为 `c = n ? p[i * 2 + 1] : 0`（`:10952`）。【确认】
- ASCII：`chr < 0x80` 原样返回（`:10933–10934`）。【确认】
- 查表：二分查找，最多 16 次迭代（`:10944–10951`）；`hi = sizeof … / 4 - 1`（`:10938`、`:10941`）。【确认】
- 双字节打包：首字节在高 8 位（`ff.c:2642–2643` `w = (w << 8) + b`；`ff.c:2497–2498` `c = c << 8 | dp->dir[i++]`；`ff.c:4965` `w = w << 8 | dp->dir[si++]`）。【确认】内存中的字节序由编译器确定，未核对。
- 单字节：表中仅 `0x80 ↔ U+20AC`（`cc936.c:59`、`:5470`）。【确认】0x81–0xFF 返回 0。【推断】（N16）
- 调用点：`ff.c` 1952、2473、2500、2646、2698、2701、4950、4967、5042、5058、5065、5883、5953。【确认】`ff.c:2496–2502` 位于 `#if _LFN_UNICODE`。【确认】其余调用点的编译条件未逐一核对。【未核对】（N19）
- 前提：`_USE_LFN 1`（`FATFS/Target/ffconf.h:112`）、`_LFN_UNICODE 1`（`:129`）、`_CODE_PAGE 936`（`:84`）。【确认】
- `ff_wtoupper` 位于同一文件 `cc936.c:10960`，使用 `cvt1`、`cvt2` 表（`:10965–10989`）。【确认】

### 5.2 表覆盖范围与统计
- 两表：`uni2oem[]`（`cc936.c:13–14` 至 `:5465`）与 `oem2uni[]`（`:5467–5468` 至 `:10919`），均为 `static const WCHAR`，每条两个 WCHAR，末尾 `0, 0`。【确认】文件头注释 "CP936 (Simplified Chinese GBK)"（`:2–3`）。【确认】
- 条目：两表各 21792 条（含单字节 1 条）。【确认】（源码直接计数）
- 分类（awk 抽取 `0x…, 0x…` 对，分类规则为本票定义）：GB2312 区（首 A1–F7 且尾 A1–FE）7478 条；GBK 扩展（其余双字节，首 81–FE、尾 40–FE 且非 7F）14313 条；用户区（AAA1–AFFE、F8A1–FEFE）0 条；单字节 1 条。【确认】（统计）7478 为表内条目数，不是 GB2312 标准字符数。
- 与 Microsoft 映射表比对：Unicode Consortium 的 `CP936.TXT`（https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP936.TXT，curl 200，817310 B，文件头 Table version 2.01、日期 1/7/2000，数据行 22047）。【确认】双字节映射 21791 条，两表全部存在且双向一致（缺失 0、不一致 0）；双字节码的 Unicode 无重复。【确认】（统计）
- 单字节：`0x00–0x7F` 为 ASCII；`0x80 → U+20AC`；`0x81–0xFF` 共 127 行 Unicode 为空（如 `CP936.TXT` 第 148 行 DBCS LEAD BYTE、第 274 行 UNDEFINED）。【确认】
- 样例：`0x8140 → U+4E02`（丂）、`0xD6D0 → U+4E2D`（中）、`0xA1A4 → U+00B7`、`0x80 ↔ U+20AC` 均在两表中。【确认】`0xA8BC`、`0xFE50`、`0xAAA1` 在 CP936.TXT 与两表中均无映射；`U+E000` 在 `uni2oem` 中无映射。【确认】
- 双字节码首字节分布（CP936.TXT，与 FatFs 表一致）：81–A0 为 6080 条（扩展）；A1–A9 为 715 条（GB2312）与 153 条（扩展）；AA–AF 为 576 条（扩展）；B0–F7 为 6763 条（GB2312）与 6912 条（扩展）；F8–FE 为 592 条（扩展）。【确认】（统计）
- 结论式：表与 Microsoft 的 CP936 双字节映射一致，包含 14313 条 GBK 扩展码位。"GBK 全集" 的标准字符数未在本票核对。【未核对】（N9）

### 5.3 表体积与链接位置
- map：`.rodata.uni2oem` 0x15484 B 位于 0xC0000000，`.rodata.oem2uni` 0x15484 B 位于 0xC00154A0，均在 `.sdram_external_resources (NOLOAD)` 段内（`build/firmware-debug/mp3_project.map` 约第 35336–35352 行；`STM32H743xG_flash.ld:87–116`，ASSERT 见 `:112–114`）。【确认】两表的链接地址在 SDRAM，不占内部 Flash 段。【确认】
- 合计 174344 B，与 `APP/tasks/storage/benchmark/README.md:53` 的 "合计 174344 B" 一致。【确认】
- 内部 Flash 的 FatFs 代码与 `cvt` 表（map 约第 40808–40816、43809–43816 行）：`.text.ff_convert` 0xBC B（0x0803DB44）、`.text.ff_wtoupper` 0x128 B（0x0803DC00）、`.rodata.cvt1.1` 0x1F2 B（0x08062DC4）、`.rodata.cvt2.0` 0xBC B（0x08062FB8）。【确认】四者合计 1170 B。【推断】（N18）
- 资源包：ResourceID 1、2 为必需 BINARY 项（`Service/resource/README.md:16`）；`Service/resource/resource_service.c:84–97` 将其复制到两段，并 `VerifyAfterCopy = true`。【确认】资源包内两表占用的 NOR 字节数未核对。【未核对】（N10）
- map 时效：map 早于 `932a685`（见第 1 节）；表大小与当前源码的计数一致，链接脚本 ASSERT 常量 0x15484 也一致。【推断】（N15；大小一致不能证明全部构建输入一致）

### 5.4 线程安全与可重入
- `ff_convert` 只使用局部变量，并读取 `static const` 表（`cc936.c:10923–10956`）。【确认】函数体不写任何全局或 static 变量。【推断】（N14）`ff_wtoupper` 同样只读 `static const` 表。【推断】（N14）
- 外部直接调用：仓库内非 FatFs 源码未找到对 `ff_convert` 的直接调用（检索 `ff_convert\s*(`：APP、Service、Platform、Adapters、Core、FATFS、Tests、Components）。【未找到】由于无共享可变状态，直接调用不改变卷状态。【推断】（N14）
- FatFs 锁：本配置 `_FS_REENTRANT 0`（`FATFS/Target/ffconf.h:241`），注释 "0: Disable re-entrancy"（0：关闭可重入）（`:250`）；`:245–248` 注明 f_mount、f_mkfs、f_fdisk 始终不可重入。【确认】`lock_fs`、`unlock_fs` 位于 `#if _FS_REENTRANT`（`ff.c:737–761`），本配置不编译。【确认】
- 官方（当前 R0.16）：`doc/appnote.html` "The file operations of two tasks to the same volume is not thread-safe by default"（同一卷上两个任务的文件操作默认不是线程安全的）（https://elm-chan.org/fsw/ff/doc/appnote.html，成功）；`doc/config.html` 的 FF_FS_REENTRANT 段（https://elm-chan.org/fsw/ff/doc/config.html，成功）。【确认（官方当前版本）】
- 官方对 `ff_convert` 无条目（检索 "convert"，仅 config 页 Unicode API 段一处）。【未找到】appnote 列出 `ff_uni2oem`、`ff_oem2uni`、`ff_wtoupper`，并写 "Just add ffunicode.c to the project"（只需把 ffunicode.c 加入工程）。【确认】与本地 R0.12c 不同（本地表与 `ff_wtoupper` 在 `cc936.c`）。

### 5.5 版本号
- 本地：`ff.h:2` "FatFs - Generic FAT file system module  R0.12c"；`ff.h:22` `#define _FATFS 68300 /* Revision ID */`。【确认】本地 `Middlewares/Third_Party/FatFs/src/00history.txt` 末条为 `R0.12c (March 04, 2017)`，共 288 行。【确认】
- 官方首页（https://elm-chan.org/fsw/ff/，200，9129 B）列出 `R0.16` 与 `arc/ff16.zip`。【确认】官方 `00history.txt` 直链（https://elm-chan.org/fsw/ff/00history.txt）返回 404。【失败】（N9）
- 结论：本地为 R0.12c；官方首页当前为 R0.16，宏名前缀不同（本地 `_CODE_PAGE`，官方 `FF_CODE_PAGE`）。【确认】

## 6. 未决与需板上测量清单

- N1【未核对】Mp3tag 默认版本与编码（官方页无默认值；论坛称偏向 2.3，【二手】）。
- N2【未核对】foobar2000 当前默认版本、编码，及兼容选项的确切行为（无一手页面）。
- N3【未核对】iTunes 当前版本是否仍写非 syncsafe 的 v2.4 帧大小（仅 TagLib 注释与 2005–2007 列表）。
- N4【需样本核对】网易云音乐、QQ 音乐下载文件的 ID3 版本与编码（无一手来源）。
- N5【未核对】ID3v2.2.0 原文（id3.org 404）；仅 mutagen-specs 页（WebFetch 摘录）与搜索发现的两个副本（javamusictag、id3lib，未取得）。
- N6【未核对】v2.3 标志字节 b（扩展头位）与 v2.4 扩展头标志的逐字原文（仅 WebFetch 摘录）。
- N7【未核对】规范对非法大小与非法编码字节的处理（原文无条款；仅有实现行为）。
- N8【未核对】TagLib `joinTagValues` 的连接符（定义位于未下载的 `tag.cpp`）。
- N9【未核对】FatFs R0.12c 的官方文档与历史（`00history.txt` 直链 404）；R0.16 与 R0.12c 在 `ff_convert` 相关处的差异未逐条核对；"GBK 全集" 标准字符数未核对。
- N10【未核对】资源包中两表占用的 NOR 字节数；map 对应的完整构建输入（map 早于 932a685 为推断，与 HEAD 的对应未证明）。
- N11【未核对】论坛、邮件列表原帖，FFmpeg ticket #3904、Ubuntu 论坛帖的原文（仅搜索摘要）。
- N12【推断】TIT2/TPE1 字节范围公式与最坏上界（2.7、4.7）；总缓冲 1 MiB（4.4）。
- N13【推断】v2.4 与 v2.3 之间帧大小的回退判定会改变偏移（3.2、4.7）。
- N14【推断】`ff_convert`、`ff_wtoupper` 无可变共享状态；外部直接调用无共享状态（源码阅读，未实测）。
- N15【推断】map 与当前构建输入一致（仅表大小一致；时间早于 932a685）。
- N16【推断】0x81–0xFF 单字节的 `ff_convert` 返回值（由表内容推导，未实测）。
- N17【推断】约 2.3 MB/s 的 NOR→SDRAM 复制速率（由 #122 数字算得，仅复制与 CRC）。
- N18【推断】内部 Flash 占用 1170 B（map 字段相加）。
- N19【未核对】`ff_convert` 其余调用点（`ff.c` 1952 等）的 `#if` 编译条件未逐一核对。
- M1【需板上测量】SD 顺序读吞吐：卡型号与容量、FAT 类型与簇大小、文件大小（多档）、读路径（`storage_sd_benchmark` 或等价）、重复次数与中位数。
- M2【需板上测量】`f_open` 耗时（同卡同文件，打开后不读，重复次数与中位数）。
- M3【需板上测量】打开并读取前 N KB 的耗时（N 取多档，含 10 字节头与 160 字节尾的读取组合）。
- M4【需板上测量】读取 TIT2/TPE1 的字节数与耗时（样本含 v2.2/2.3/2.4、不同帧顺序、APIC 前置、非 syncsafe 大小）。
- M5【需板上测量】ID3v1 定位（-128）与读取耗时。
- M6【需板上测量】1 MB ID3 标签文件的跳过耗时（板测清单 A9）。
- M7【需板上测量】SD 读与音频流并发时的欠载次数与块池余量（1 MiB）。
- M8【需板上测量】`storage_sdram_benchmark` 输出（默认关闭，仅在需要时测）。
- M9【需板上测量】`ff_convert` 单次调用耗时与双字节查表耗时。
- M10【需板上测量】Queue 或唱盘页的读取时延（仓库内无实测）。

## 7. 来源清单

访问日期均为 2026-10-27。
- Issue #129：https://github.com/immorcoding/mp3_project/issues/129（gh 只读，成功，评论 0）。
- id3.org 索引：https://id3.org/（200，12495 B；索引链接不含 ID3v1 与 v2.4 frames 页）。
- ID3v2.3.0：https://id3.org/id3v2.3.0（200，176720 B）；ID3v2.4.0 结构：https://id3.org/id3v2.4.0-structure（200，97401 B）；帧：https://id3.org/id3v2.4.0-frames（200，222349 B）；变更：https://id3.org/id3v2.4.0-changes（200，24894 B）；ID3v1：https://id3.org/ID3v1（200，16583 B）。
- ID3v2.2.0：https://id3.org/id3v2.2.0（404，失败）。mutagen-specs v2.2：https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html（WebFetch 成功，副本）。搜索发现的副本（未取得）：javamusictag.sourceforge.net、id3lib.sourceforge.net。
- mutagen（`452190d42ab2092577c77342d49157f8768d796f`，release-1.48.1）：https://github.com/quodlibet/mutagen/blob/452190d42ab2092577c77342d49157f8768d796f/mutagen/id3/_tags.py（首次 TLS 失败，重试成功）、`_frames.py`、`_util.py`、`_specs.py`、`_file.py`（同 commit，raw 下载成功）。
- TagLib（`deadc2990767dfbda0701e0ab35fdeea653db08f`，v2.3.2）：https://github.com/taglib/taglib/blob/deadc2990767dfbda0701e0ab35fdeea653db08f/taglib/mpeg/id3v2/id3v2tag.cpp（及 `id3v2frame.cpp`、`id3v2synchdata.cpp`、`id3v2header.cpp`、`id3v2framefactory.cpp`，后者首次 TLS 失败、重试成功）；`taglib/mpeg/id3v1/id3v1tag.cpp`（同 commit）。
- Mp3tag：https://docs.mp3tag.de/customization/options/tags/mpeg/（curl 200，10787 B；WebFetch 失败：域名被工具安全检查拒绝）；存档：https://web.archive.org/web/20260118104131/https://docs.mp3tag.de/customization/options/tags/mpeg/（200，14570 B）。
- foobar2000 更新日志：https://www.foobar2000.org/changelog（WebFetch 成功）。
- 二手（搜索摘要，未逐页核对）：https://hydrogenaudio.org/index.php/topic,57198.0.html；https://lists.id3.org/pipermail/id3v2/2007-February/000137.html；https://trac.ffmpeg.org/ticket/3904；https://forum.ubuntu.org.cn/viewtopic.php?p=726208；https://code.google.com/p/mp3tagiconv
- FatFs：官方首页 https://elm-chan.org/fsw/ff/（200，9129 B）；配置 https://elm-chan.org/fsw/ff/doc/config.html（200，21812 B）；应用说明 https://elm-chan.org/fsw/ff/doc/appnote.html（200，41224 B）；历史 https://elm-chan.org/fsw/ff/00history.txt（404，失败）。
- Unicode CP936：https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP936.TXT（200，817310 B）。
- GitHub 评论与正文：#81 https://github.com/immorcoding/mp3_project/issues/81#issuecomment-6274815372（及 …#issuecomment-6274816505）；#110 https://github.com/immorcoding/mp3_project/issues/110#issuecomment-6348004552；#122 https://github.com/immorcoding/mp3_project/issues/122#issuecomment-6432651748；#62 https://github.com/immorcoding/mp3_project/issues/62。
- 仓库路径（worktree，均相对 `E:\Projects\project_mp3\source_code\.claude\worktrees\research-id3`）：`APP/tasks/storage/stream/`、`APP/tasks/storage/benchmark/`、`APP/app_config.h`、`APP/tasks/storage/storage_task.c`、`Platform/audio/`、`Service/playback/`、`Service/resource/`、`Middlewares/Third_Party/FatFs/src/`、`FATFS/Target/ffconf.h`、`STM32H743xG_flash.ld`、`Tests/storage_audio_stream/`、`docs/`、`GLOSSARY.md`；主检出只读：`build/firmware-debug/mp3_project.map`。
