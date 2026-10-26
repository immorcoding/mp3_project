# ResourcePack Component

本 Module 实现与 MCU、Flash 和业务无关的 RPKC1 只读解析。它显式读取小端字段，打开包时校验 Header CRC、Entry 排序、公共字段、范围、对齐和任意 Metadata/Data 重叠；大型 Data 只在使用前按需校验。

## 公开 Interface

- `ResourcePack_Open()` 打开一段调用者保证可读的内存窗口，不申请动态内存。
- `GetInfo/GetEntryCount/GetEntry/FindEntry` 查询公共头和严格升序 Entry。
- `GetEntryView` 返回映射窗口内的临时只读 Metadata/Data View。
- `VerifyMetadata/VerifyBuffer` 分别校验 Metadata 公共前缀与任意数据 CRC。
- `DecodeBinaryMetadata/DecodeImageMetadata/DecodeFontMetadata` 解码启用的三种 Metadata V1。
- `ResourcePack_DecodeFontMetadata()` 解码 FONT V1：Metadata 固定 32 B，条目 Data 是 `lv_font_conv` 1.5.3 未压缩 bin 的原样字节。`FontFormat`、`CodepointEncoding` 的枚举值（`RESOURCE_PACK_FONT_FORMAT_LV_FONT_CONV_BIN`、`RESOURCE_PACK_CODEPOINT_ENCODING_UNICODE`）与位图总量上限 `RESOURCE_PACK_FONT_BITMAP_LIMIT_BYTES`（1 MiB，不含）在公开头。

## 编译期依赖和运行路径

Implementation 只依赖 ISO C 和本 Module 头文件，不包含 W25Qxx、HAL、Platform、Service、FatFs、LVGL 或 FreeRTOS。运行时由上层把可读内存窗口交给 Component；Component 不发起硬件请求，也不持有窗口生命周期。

## 类型裁剪与约束

`resource_pack_config.h` 只裁剪类型专用 Metadata 解码能力。BINARY、IMAGE 和 FONT 默认启用，AUDIO、MODEL、FIRMWARE 默认关闭；关闭或未知类型不阻止 Core 打开满足公共规则的包，实际解码返回 `RESOURCE_PACK_UNSUPPORTED_TYPE`。

FONT 解码在核对 Metadata 之外顺序扫描一遍 bin 的 head、cmap、loca、glyf 分段，不申请内存、不建字形表：`GlyphCount` 须等于 loca 条目数（含索引 0 的 notdef），`NominalPixelHeight`、`Ascent`、`Descent` 须等于 head 中的字号与行高，位图总量（口径同 `lv_font_load`：不计字形 0 与空盒子字形）达到 1 MiB 时按格式错误拒绝，因为 `LV_FONT_FMT_TXT_LARGE = 0` 时位图索引只有 20 位。字形表、cmap 和位图重排由字体消费者解析；调用者提供的 View 必须覆盖完整 bin，扫描会读取 loca 与每个字形的头部。

所有整数显式按小端读取，禁止把落盘数据强转为 C 结构体。Handle 不复制 Entry 表，Find 使用二分查找。完整协议、编号空间和兼容规则见 [RPKC1 设计](../../docs/shape/resources.md)。
