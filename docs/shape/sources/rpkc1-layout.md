# RPKC1 容器数据字典

[Resources](../resources.md) 的互操作依据；实现与身份维护策略见 RES-4/RES-7，[扩展类型](rpkc1-extensions.md)独立定义尚未启用的载荷。下列大小以字节计，Offset 相对所属结构起点。

## 编码契约

| 项目 | 唯一编码或有效范围 |
| --- | --- |
| 标识与数值 | Magic = ASCII RPKC（52 50 4B 43），FormatVersion = 1；整数小端；PackSize/Offset/Length 为32位，包小于4 GiB。 |
| 包布局 | Header = 64 B 前缀 + Entry 表 + padding + 尾部 CRC；Body 按 ResourceID 顺序依次放各项 Metadata、对齐填充、Data。Padding 与未用区域均为 FF。 |
| Entry 表 | 有效项由 EntryCount 决定，无空记录；EntryOffset + EntryCount × EntrySize ≤ HeaderSize − 4。ResourceID 非零、唯一、升序。 |
| Body 区间 | 非空 Metadata/Data 完整位于 [HeaderSize, PackSize)，任意两项互不重叠，包括完全共享的 Data；DataLength > 0。 |
| 对齐 | DataOffset/MetadataOffset 分别服从 Header 声明的对齐，两种对齐均为2的幂，DataAlignment ≥ 4。 |
| 空 Metadata | Offset、Length、CRC 同时为0；只有 BINARY 可省略，其他已定义类型必需。 |
| 非空 Metadata | 8 B 公共前缀；MetadataSize = Entry.MetadataLength；MetadataVersion 属于类型自身，独立于容器版本；未知版本不兼容。 |
| 公共属性 | Flags = 0，无 Required/Preload/MemoryMapped 位；无公共字符串表，名称可作为类型专有 UTF-8 Metadata。 |
| CRC 算法 | CRC-32/ISO-HDLC；反射多项式 EDB88320，初值 FFFFFFFF，输入/输出均反射，最终异或 FFFFFFFF。 |
| Header CRC | 覆盖 [0, HeaderSize − 4)，含 Entry 与 Header padding；结果占最后4 B，与 HeaderSize 一起移动。 |
| 资源 CRC | Entry 保存对应 Metadata/Data 各自范围的 CRC；不在 Metadata 内重复；无整包 CRC，Body padding 不参与。 |
| 子格式32位空间 | 00000000 = INVALID/UNKNOWN；00000001–7FFFFFFF = 标准；80000000–FFFFFFFE = 自定义；FFFFFFFF = 保留。适用于图片/像素/字体/音频/容器/模型/固件子格式。 |
| BINARY V1 | 可省略 Metadata 的 OPAQUE 数据；非 OPAQUE 数组满足 ElementSize × ElementCount = DataLength。编码转换数组属于 BINARY，字形才属于 FONT。 |
| IMAGE V1 | Width/Height/FrameCount > 0；RAW/LVGL 像素必须声明 PixelFormat/StrideBytes，PNG/JPEG 可为0；FrameCount = 1 是静态，多帧声明不承诺解码能力。 |

## 字段字典

| 结构 | Offset | Size | Field | 约束 |
| --- | ---: | ---: | --- | --- |
| Header | `0x00` | 4 | Magic | ASCII `RPKC` |
| Header | `0x04` | 4 | FormatVersion | RPKC1 为 1 |
| Header | `0x08` | 4 | HeaderSize | 当前为 `0x1000` |
| Header | `0x0C` | 4 | PackSize | 完整包长度 |
| Header | `0x10` | 4 | EntryOffset | 当前为 `0x40` |
| Header | `0x14` | 2 | EntryCount | 有效 Entry 数量 |
| Header | `0x16` | 2 | EntrySize | RPKC1 必须为 40 |
| Header | `0x18` | 4 | DataAlignment | 当前 W25Q256 包为 4096 |
| Header | `0x1C` | 4 | MetadataAlignment | 当前为 4 |
| Header | `0x20` | 4 | VendorID | 由产品配置决定 |
| Header | `0x24` | 4 | ProductID | 由产品配置决定 |
| Header | `0x28` | 8 | PackageVersion | 单调递增 |
| Header | `0x30` | 16 | Reserved | 必须全为 `0xFF` |
| Entry | `0x00` | 4 | ResourceID | 非零、包内唯一、严格升序 |
| Entry | `0x04` | 2 | ResourceType | 见标准类型表 |
| Entry | `0x06` | 2 | Flags | RPKC1 必须为 0 |
| Entry | `0x08` | 8 | ResourceVersion | 同一 ID 的内容版本 |
| Entry | `0x10` | 4 | DataOffset | 相对包首地址 |
| Entry | `0x14` | 4 | DataLength | 必须大于零 |
| Entry | `0x18` | 4 | DataCRC32 | 只覆盖 Data |
| Entry | `0x1C` | 4 | MetadataOffset | 相对包首地址；无 Metadata 时为 0 |
| Entry | `0x20` | 4 | MetadataLength | 无 Metadata 时为 0 |
| Entry | `0x24` | 4 | MetadataCRC32 | 无 Metadata 时为 0 |
| Metadata prefix | `0x00` | 4 | MetadataVersion | — |
| Metadata prefix | `0x04` | 4 | MetadataSize | — |
| BINARY V1 | `0x00` | 4 | MetadataVersion = 1 | — |
| BINARY V1 | `0x04` | 4 | MetadataSize = 24 | — |
| BINARY V1 | `0x08` | 4 | ElementFormat | — |
| BINARY V1 | `0x0C` | 4 | ByteOrder | — |
| BINARY V1 | `0x10` | 4 | ElementSize | — |
| BINARY V1 | `0x14` | 4 | ElementCount | — |
| IMAGE V1 | `0x00` | 4 | MetadataVersion = 1 | — |
| IMAGE V1 | `0x04` | 4 | MetadataSize = 40 | — |
| IMAGE V1 | `0x08` | 4 | ImageFormat | — |
| IMAGE V1 | `0x0C` | 4 | PixelFormat | — |
| IMAGE V1 | `0x10` | 4 | Width | — |
| IMAGE V1 | `0x14` | 4 | Height | — |
| IMAGE V1 | `0x18` | 4 | StrideBytes | — |
| IMAGE V1 | `0x1C` | 4 | FrameCount | — |
| IMAGE V1 | `0x20` | 4 | ColorSpace | — |
| IMAGE V1 | `0x24` | 4 | AlphaMode | — |

## 类型编码

| Value | Type | 含义 |
| ---: | --- | --- |
| `0x0000` | INVALID | 无效 |
| `0x0001` | BINARY | 通用不透明数据或数组 |
| `0x0002` | FONT | 可提供字符视觉数据的字体资源 |
| `0x0003` | IMAGE | 编码图片或原始像素 |
| `0x0004` | AUDIO | 容器、压缩音频或原始 PCM |
| `0x0005` | MODEL | 推理模型及 Tensor 描述 |
| `0x0006` | FIRMWARE | 供独立升级 Service 使用的固件载荷 |
| `0x0007–0x7FFF` | Reserved Standard | 后续 RPKC 标准类型 |
| `0x8000–0xFFFE` | Custom | 用户或厂商自定义类型 |
| `0xFFFF` | Reserved | 保留 |
