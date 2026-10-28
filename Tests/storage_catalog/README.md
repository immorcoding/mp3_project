# Storage 曲库主机测试

本目录通过 `storage_catalog_name.h` 验证曲目显示名纯函数：取最后一个 `/` 之后的部分，只去掉末尾 `.mp3`（`m`、`p` 两位大小写任意，与曲库扫描 `storage_catalog_music_file_strcheck` 接受的形式一致），其余原样保留；只有扩展名的文件（如 `.mp3`）保留整名；主名超过 251 B 或不是合法 UTF-8 时失败，不截断、不写半个字符，失败时输出缓冲为空串。只编译 `storage_catalog_name.c`，不链接 FreeRTOS、FatFs、HAL 或曲库字符串池。

第二个测试程序 `storage_catalog_text_tests` 通过 `storage_catalog_text.h` 验证曲目文本纯函数，也是 `Components/id3_reader` 的唯一测试接缝（不单开模块）。它用字节数组构造假文件，经“按偏移读”回调提供，并链接真实 `cc936.c`（复制 `FATFS/Target/ffconf.h` 并去掉 MCU 头）实现 GBK 查表。覆盖：v2.2 / v2.3 UTF-16LE / v2.4 UTF-8；真 Latin-1、声明 Latin-1 实为 GBK、结构合法但无映射的码位回退 Latin-1；缺 BOM、BE BOM、多值各自 BOM、代理对；多值连写、`AC/DC`、首尾空白与 U+3000、全空白回退、NUL 填充；超长按字符边界截断与截断后仍校验；非法 UTF-8 / 孤立代理 / 奇数长度 / 编码字节 > 3 / 控制字符 / 压缩或加密帧；v2.4 帧级去同步、数据长度指示、分组字节，扩展头；帧顺序不固定与重复帧；无标签、帧缺失、非法帧 ID、帧越过标签末尾、标签头不可读；首块读取偏移与长度、APIC 前置跨首块 seek 且不越过标签总长、帧负载跨块边界、恰 4 次读取与超读取上限、读取错误注入、首块读入的标签之后字节（形似 TIT2/TPE1 帧）不被解析、标签级去同步 v2.3（含超出首块）、TIT2 可用而 TPE1 坏、文件主名提取失败的防御分支。

该程序还含一条编译期断言：`STORAGE_CATALOG_TRACK_BUDGET_US` 等于 `PLAYBACK_MACHINE_DATA_TIMEOUT_MS` × 1000 / 2（Storage 与 Playback 的 config 互不 include，只在此处同时可见）；`playback_task_config.h` 变化因此也选中本模块。

保真边界：PASS 只证明提取规则本身。曲库扫描（`storage_catalog.c`）、名称单槽握手与 Storage 任务外壳不做主机测试，进板级验收；扩展名大小写与扫描代码的一致性靠人工对照，不由测试自动保证。曲目文本函数的 PASS 只证明规则链与读取计划（偏移、次数、不越过标签）；不证明 FatFs 实际读取、`f_open` 失败判定、SD 耗时与单行时延预算，也不证明板上 CP936 表已就绪或 `Service_Filesystem_GbkToUnicode` 的接线，这些进板级验收（#135 I1–I13）。测试里的 GBK 回调直接调 `ff_convert`，与生产薄函数同源但不是它。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module storage_catalog
```
