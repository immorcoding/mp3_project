# GUI 字体解码 Module

把资源包 FONT 条目里的 `lv_font_conv` 1.5.3 未压缩 bin 解码成 LVGL 8.3 `fmt_txt` 字体。它是 `Service/gui` 的私有 Module，不属于 `Service_GUI_*` 公开 Interface，APP 与其他 Service 不得包含。

本 Module 目前只提供解码器本身；`Service_GUI_Init()` 的接线、Resource Service 的字体查询、链接脚本里的解码区与模拟器的替身另有任务，尚未接入。

## 私有 Interface

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

尺寸只取决于输入，不取决于调用顺序。提交的真实字体（7541 个字形、55 个 cmap 子表、517698 B 位图、4488 个字距对）需要 621,024 B（Cortex-M7，32 位指针估算；主机 64 位指针为 621,712 B）。

## 与 `lv_font_load` 的差别

结果在字形描述、位图、字距上与 `lv_font_load` 逐项一致，位图逐字节一致（非整字节字形头时整体左移 `位数 % 8`，这是真实字体的情形：9 + 2*5 + 2*4 = 27 位）。刻意不同的有两点：

- **FORMAT0 子表输出为 SPARSE 子表。** LVGL 8.3 的码点查找用 `rcp > range_length` 判断范围，码点恰为范围末尾的下一个时，`FORMAT0_TINY` 返回邻近字形（按代码推得，真实字体里 U+007F 会得到 `¤`，未在 LVGL 上运行验证），`FORMAT0_FULL` 还会多读列表外一项；稀疏表的二分查找不受影响。这让字集外的相邻字符稳定画占位框，而不是画成另一个字。代价是码点列表多约 2–4 B/字符，真实字体共 19,404 B。
- **先完整校验再写输出区。** `lv_font_load` 边读边分配，出错时留下半成品。

没有 kern 分段（`tables_count` 小于 4）时字体不带字距。kern 类别映射在输出区补零到字形数，LVGL 以任意合法字形编号索引都不越界。不实现压缩位图（`LV_USE_FONT_COMPRESSED = 0`）；`LV_FONT_FMT_TXT_LARGE = 0`，位图总量不小于 1 MiB 的字体被拒绝（口径同 `RESOURCE_PACK_FONT_BITMAP_LIMIT_BYTES`）。

## 验证

`Tests/gui_font` 用最小 `lvgl.h` 替身编译本 Module 与 Component，不链接 LVGL。保真边界见 [Tests/gui_font/README.md](../../../Tests/gui_font/README.md)：主机 PASS 证明解码结果与独立参照一致、失败输入干净地失败；不证明 LVGL 真实渲染、SDRAM/Cache 行为或板上解码耗时。
