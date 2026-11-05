# Storage 曲库主机测试

本目录通过 `storage_catalog_name.h` 验证曲目显示名纯函数：取最后一个 `/` 之后的部分，只去掉末尾 `.mp3`（`m`、`p` 两位大小写任意，与曲库扫描 `storage_catalog_music_file_strcheck` 接受的形式一致），其余原样保留；只有扩展名的文件（如 `.mp3`）保留整名；主名超过 251 B 或不是合法 UTF-8 时失败，不截断、不写半个字符，失败时输出缓冲为空串。只编译 `storage_catalog_name.c`，不链接 FreeRTOS、FatFs、HAL 或曲库字符串池。

第二个测试程序 `storage_catalog_text_tests` 通过 `storage_catalog_text.h` 验证曲目文本纯函数，也是 `Components/id3_reader` 的唯一测试接缝（不单开模块）。它用字节数组构造假文件，经“按偏移读”回调提供，并链接真实 `cc936.c`（复制 `FATFS/Target/ffconf.h` 并去掉 MCU 头）实现 GBK 查表。覆盖：v2.2 / v2.3 UTF-16LE / v2.4 UTF-8；真 Latin-1、声明 Latin-1 实为 GBK、结构合法但无映射的码位回退 Latin-1；缺 BOM、BE BOM、多值各自 BOM、代理对；多值连写、`AC/DC`、首尾空白与 U+3000、全空白回退、NUL 填充；超长按字符边界截断与截断后仍校验；非法 UTF-8 / 孤立代理 / 奇数长度 / 编码字节 > 3 / 控制字符 / 压缩或加密帧；v2.4 帧级去同步、数据长度指示、分组字节，扩展头；帧顺序不固定与重复帧；无标签、帧缺失、非法帧 ID、帧越过标签末尾、标签头不可读；首块读取偏移与长度、APIC 前置跨首块 seek 且不越过标签总长、帧负载跨块边界、恰 4 次读取与超读取上限、读取错误注入、首块读入的标签之后字节（形似 TIT2/TPE1 帧）不被解析、标签级去同步 v2.3（含超出首块）、TIT2 可用而 TPE1 坏、文件主名提取失败的防御分支。

该程序还含一条编译期断言：`STORAGE_CATALOG_TRACK_BUDGET_US` 等于 `PLAYBACK_MACHINE_DATA_TIMEOUT_MS` × 1000 / 2（Storage 与 Playback 的 config 互不 include，只在此处同时可见）；`playback_task_config.h` 变化因此也选中本模块。

第三个测试程序 `storage_catalog_open_tests`（`test_storage_catalog_open.c`，STOR-17、ARC-19、DIAG-4、DIAG-5，#179）链接真实 `storage_catalog.c`、`storage_sheet.c`、`storage_track_open.c`、`storage_track_text.c`、`storage_track_diag.c` 与曲目文本纯函数，只把 Filesystem Service 公开函数（目录扫描、`OpenFile`、`OpenFileByLocator`、读/seek/关闭）、Log Service 与周期计数器 Adapter 换成测试文件里的假实现（合成曲库，定位符由文件 id 派生；假 DWT 为 1 MHz，周期即微秒）。覆盖：曲库上限 12000 的编译期与运行期核对（12500 条目扫描后恰收录 12000 条、末条定位符完好、超出截断仍成功并记一次 `Music Catalog Full.`）、池大小与每首可用路径空间、新阈值常量（单行打开 30 ms、整窗 0.5 s）及与 T_data/2 防御阈值的次序；扫描时每条（含子文件夹内）随附正确的定位符，定位符只在同一曲库代次内可取；按曲库下标读曲目文本走定位符而不走路径，打开耗时单独给出；核对不符（介质已变）与扫描时没取到定位符（全零）都退回按路径打开同一首、两次尝试都计入打开耗时、记一行 `Locator miss pos=N` WARN（位置取播放列表位置而非曲库下标，每次各记一行）；其他失败（槽用尽）不退回路径、不记 miss、回退文件主名；下标越界返回空；名称槽 INFO 带 `open=` 且 `us=` 不含打开；窗口汇总行在单行打开恰 30 ms、整窗恰 0.5 s 时静默，单行打开 30001 µs、整窗 500004 µs、WARN 级原因、T_data/2 单行防御阈值任一出现时触发，汇总行放得进 Service Log 的 128 B；`Stream pos=N off=<B> open=<us> lseek=<us>` 行格式；扫描结束 INFO `Scan done res=<ok|dfs_error|sheet_error> n=<条数> us=<微秒>`（#204，DIAG-6）：格式与 tag、耗时取目录读取花掉的假 DWT 时间（含计数器在扫描中途 32 位回绕）、每次重扫各一行且按本轮算、没有 `Music/` 为 `ok n=0`、12500 条截断为 `ok n=12000`、目录读取失败为 `dfs_error`；卷不可用（`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE`，#211）：打开 `Music/`、打开子目录或扫描到一半读目录得到它们时，丢弃本次扫描、不发布播放列表（曲库保持无效）、不记 ERROR、结束行写 `res=aborted n=0`，其他失败（`SERVICE_ERROR`）仍按空库发布；曲目标签读取打开时得到它们返回空结果，不回退文件主名、不退回路径打开。每个用例末检查没有泄漏的打开句柄。

保真边界：PASS 只证明提取规则本身。名称单槽握手、Queue 窗口逐圈填写与 Storage 任务外壳不做主机测试，进板级验收；曲库扫描只证明“随条目写入定位符”与上限截断，不证明真实目录扫描耗时；真实按定位符打开由 `Tests/filesystem_locator` 证明，这里的假 `OpenFileByLocator` 只复现三态约定，不证明它与 FatFs 的核对一致；开流路径（`storage_audio_stream.c`）改用同一打开入口，但该文件含信箱与 FatFs 盖章，不做主机测试，`Stream` 行只钉格式，其耗时取值与 `open`/`lseek` 的口径由板级验收（O3、O4）取证；扩展名大小写与扫描代码的一致性靠人工对照，不由测试自动保证。曲目文本函数的 PASS 只证明规则链与读取计划（偏移、次数、不越过标签）；不证明 FatFs 实际读取、`f_open` 失败判定、SD 耗时与单行时延预算，也不证明板上 CP936 表已就绪或 `Service_Filesystem_GbkToUnicode` 的接线，这些进板级验收（#135 I1–I13）。测试里的 GBK 回调直接调 `ff_convert`，与生产薄函数同源但不是它。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module storage_catalog
```
