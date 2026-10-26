# GUI 字体 Module

把资源包 FONT 条目里的 `lv_font_conv` 1.5.3 未压缩 bin 解码成 LVGL 8.3 `fmt_txt` 字体，并决定曲名 Label 用哪个字体。它是 `Service/gui` 的私有 Module，不属于 `Service_GUI_*` 公开 Interface，APP 与其他 Service 不得包含。

两个文件：`gui_service_font.c` 是曲名字体子模块（启动接线与回退），`gui_service_font_decoder.c` 是纯解码器。

## 曲名字体子模块

- `service_gui_font_init()`：只在 GUI Task 的 `Service_GUI_Init()` 中、创建 Screen 之前调用一次。经 `Service_Resource_GetFont()` 查询可选 FONT 载荷；可用时先 Measure、核对解码区容量，再 Build 到解码区，本模块持有得到的 `lv_font_t`。成功记一条 INFO `Title font ready: glyphs=N, bytes=B, decode=T us.`；载荷不可用或解码失败记一条 WARN `Title font fallback: <原因>.`（原因为 `resource unavailable` 或解码器的状态短语）。不返回错误，GUI 照常启动。
- `service_gui_font_title()`：曲名 Label 的字体。解码成功为 Noto Sans CJK SC 12 px，否则 `lv_font_montserrat_12`。`view/` 创建曲名 Label 时取它的字体与行高；其他 Label 一律不用它（GUI-22）。

缺字由已启用的 `LV_USE_FONT_PLACEHOLDER` 画占位框，不记日志。日志经 `Service_Log_Post()` 投递（GUI Task 在调度器启动后运行，DIAG-1）；解码耗时用 DWT 周期计数器只读测量（计数器由 `app_init()` 在 Resource 加载前启动，本模块不复位）。

### 内存与 HWD-2 审查

| 区 | 所有者 | 写入时机 | 读者 |
|---|---|---|---|
| `.sdram_font_resource`（`__external_resource_font_*`，0x94800 B） | Resource Service | `Service_Resource_Init()`：正式 SDRAM 初始化后、任务启动前复制，校验目标 CRC 后 Clean | 本模块在 `Service_GUI_Init()` 中只读一次 |
| `.sdram_gui_font`（`__gui_font_decode_*`，0x98000 B） | 本模块 | `Service_GUI_Init()`：GUI Task 中、曲名 Label 创建前，解码器一次写完 | LVGL 渲染（GUI Task，CPU） |

- 启动顺序：两区都是 NOLOAD，内容在各自所有者写入前无效；FONT 区在消费者任务启动前填好，解码区在第一次渲染前填好。破坏性 SDRAM 诊断只在业务占用前运行，与两区不重叠使用。
- MPU：两区都在 `0xC0000000` 32 MiB 区域内（可缓存、写回、不可执行），只放数据，不需要新区域。
- Cache：FONT 区由 Resource Service 复制后 Clean（RES-6），本模块经 Cache 只读，不需要 Invalidate（同一 CPU 写读）。解码区只经 CPU 读写，LVGL 软件渲染逐字形读取位图，不作 DMA2D 或 SPI DMA 的源，所以不做 Cache 维护。两区按 32 B 对齐，大小是 32 的倍数，Clean 取整不越出自身归属。
- 容量：FONT 区 0x94800 = 608,256 B，入库字体 bin 606,504 B；解码区 0x98000 = 622,592 B，当前字体在 Cortex-M7 上需要 618,952 B（主机 64 位 619,640 B）。链接脚本断言两区大小；运行期仍按区大小核对（Resource 核对目标容量，本模块核对 Measure 结果），超出时走可选资源 WARN 或字体回退。

## 解码器私有 Interface

- `service_gui_font_decode_measure(bin, bin_size, info)`：完整校验 bin，不写任何输出，给出 `info->RegionSize`（解码输出区所需字节数，已按 8 B 取整）以及字形数、cmap 子表数、位图字节数、kern 字距对数。
- `service_gui_font_decode_build(bin, bin_size, region, region_size, font, info)`：校验、检查容量、再写 `region`，并填写调用者持有的 `lv_font_t`。`region` 首地址须按 `SERVICE_GUI_FONT_DECODE_REGION_ALIGN`（8 B）对齐。`info` 可为 `NULL`。
- `service_gui_font_decode_status_text(status)`：返回简短英文原因，供启动 WARN 使用。

结果用 `Service_GUI_FontDecodeStatusTypeDef`：`OK`、`INVALID_PARAM`、`FORMAT_ERROR`（截断，或表长、偏移、字形头相互矛盾）、`UNSUPPORTED`（压缩位图、子像素、`bpp` 不是 1/2/3/4/8、未知 cmap 或 kern 格式）、`CODEPOINT_RANGE`（码点超出 Unicode，或稀疏列表项超出其范围）、`BITMAP_TOO_LARGE`（位图总量不小于 1 MiB）、`OUTPUT_TOO_SMALL`。

失败一律发生在写入之前：`region` 与 `font` 保持原样。只可在 GUI Task 调用。不使用 LVGL 堆，不分配内存，不访问硬件，不记日志，不依赖 FreeRTOS。

## 与 Component 的分工

分段框架（标签、长度、范围）、head 参数的自洽与 loca 条目数由 `Components/resource_pack` 的 `ResourcePack_LocateFontBin()` 给出，`ResourcePack_DecodeFontMetadata()` 用的是同一份实现，所以 Component 与本 Module 对 bin 的结构认定一致。本 Module 在其上解析 cmap、loca 偏移、字形头、位图与 kern 的内容；Component 不依赖 LVGL（RES-1），这些输出结构只存在于这里。

读位串的小函数在两处各有一份：Component 的那份只服务位图总量统计，本 Module 要同时读有符号字段和前进宽度，口径不同，没有共用价值。

## 输出区布局

输出区内按下表顺序排列，每块按 8 B 对齐；`lv_font_t` 由调用者持有，`font->dsc` 指向输出区首块。

| 块 | 内容 |
|---|---|
| 字体描述头 | `lv_font_fmt_txt_dsc_t`，`cache` 为 `NULL`、位图格式为 `PLAIN` |
| cmap 子表 | `lv_font_fmt_txt_cmap_t`，与 bin 中的子表一一对应 |
| 码点列表 | 各子表的 `unicode_list`，`FULL` 类型再接 `glyph_id_ofs_list`（均为 `uint16_t`） |
| 字形描述 | `lv_font_fmt_txt_glyph_dsc_t`，数量等于 loca 条目数（含索引 0 的 notdef） |
| kern | 字距对描述加编号与值数组，或类别描述加左右映射与值表；没有 kern 分段时无此块 |
| 位图 | 重排后的位图，字形之间首尾相接 |

尺寸只取决于输入，不取决于调用顺序。提交的真实字体（7541 个字形、55 个 cmap 子表、517698 B 位图、4488 个字距对）需要 618,952 B（Cortex-M7，32 位指针估算；主机 64 位指针为 619,640 B）。

## 与 `lv_font_load` 的差别

结果在字形描述、位图、字距上与 `lv_font_load` 逐项一致，位图逐字节一致（非整字节字形头时整体左移 `位数 % 8`，这是真实字体的情形：9 + 2*5 + 2*4 = 27 位）。刻意不同的有两点：

- **FORMAT0 子表输出为 SPARSE 子表。** LVGL 8.3 的码点查找用 `rcp > range_length` 判断范围，码点恰为范围末尾的下一个时，`FORMAT0_TINY` 返回邻近字形（按代码推得，真实字体里 U+007F 会得到 `¤`，未在 LVGL 上运行验证），`FORMAT0_FULL` 还会多读列表外一项；稀疏表的二分查找不受影响。这让字集外的相邻字符稳定画占位框，而不是画成另一个字。代价是码点列表多约 2–4 B/字符，真实字体共 17,336 B。
- **FORMAT0_FULL 的空位不进列表。** `lv_font_conv` 允许 FORMAT0_FULL 子表带空位，字集外码点的偏移写 0，与范围首字相同；LVGL 8.3 直接查这种子表会把空位画成范围首字。字形编号随码点递增且互不相同，所以首项之后偏移为 0 的码点只能是空位，转换时跳过。真实字体的 U+951D–U+957F 子表有 21 个这样的空位，例如镕（U+9555）原先会画成锝（U+951D），现在画占位框；整个基本平面能查到字形的码点恰为生成脚本的 7540 个字符（修正前是 8057 个，多出的 517 个都是空位误配）。模拟器缺字场景（`music_title_missing_glyph`）在真实 LVGL 渲染中确认。
- **先完整校验再写输出区。** `lv_font_load` 边读边分配，出错时留下半成品。

没有 kern 分段（`tables_count` 小于 4）时字体不带字距。kern 类别映射在输出区补零到字形数，LVGL 以任意合法字形编号索引都不越界。不实现压缩位图（`LV_USE_FONT_COMPRESSED = 0`）；`LV_FONT_FMT_TXT_LARGE = 0`，位图总量不小于 1 MiB 的字体被拒绝（口径同 `RESOURCE_PACK_FONT_BITMAP_LIMIT_BYTES`）。

## 验证

`Tests/gui_font` 用最小 `lvgl.h` 替身编译本 Module 与 Component，不链接 LVGL。保真边界见 [Tests/gui_font/README.md](../../../Tests/gui_font/README.md)：主机 PASS 证明解码结果与独立参照一致、失败输入干净地失败；不证明 LVGL 真实渲染、SDRAM/Cache 行为或板上解码耗时。
