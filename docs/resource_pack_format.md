# RPKC1 二进制格式

供打包器与解码器共同使用的兼容性契约；当前启用范围、启动加载和工具操作见 [资源包设计](resource_pack_design.md)。定义格式不代表对应类型解码器已经实现。

## 3. RPKC1 总体格式

### 3.1 名称、字节序与基本约束

- 协议名称：RPKC1，`C` 表示 Common。
- 落盘 Magic：ASCII `RPKC`，即字节 `52 50 4B 43`。
- `FormatVersion`：32 位整数 `1`。
- 所有整数固定使用小端编码。
- 禁止把包地址直接强制转换为 C 结构体指针；Component 必须显式逐字段读取 `u16/u32/u64`。
- `PackSize`、Offset 和 Length 均为 32 位，单包小于 4 GiB。
- 所有范围加法和乘法先提升到 64 位再检查，禁止 32 位溢出绕过边界。
- Header、Metadata、Data 和 Entry 区域不得非法重叠。
- 有效 Entry 的 `DataLength` 必须大于零；Metadata 可以为空。
- 所有 Padding 和未使用区域填 `0xFF`。

### 3.2 逻辑布局

```text
RPKC1 Header
  -> 64 B 固定 Header 前缀
  -> Entry[EntryCount]，按 ResourceID 严格升序
  -> Header Padding = 0xFF
  -> Header CRC32

Pack Body
  -> Resource 0 Metadata
  -> 按 DataAlignment 对齐
  -> Resource 0 Data
  -> 按 MetadataAlignment 对齐
  -> Resource 1 Metadata
  -> 按 DataAlignment 对齐
  -> Resource 1 Data
  -> ...
```

Entry、Metadata 和 Data 均按 ResourceID 顺序生成，使相同输入得到确定性的二进制。Entry 顺序用于重复 ID 检查和二分查找，资源数据不依赖 JSON 中的书写顺序。

当前 W25Q256 包实例使用：

```text
HeaderSize        = 0x1000
EntryOffset       = 0x40
EntrySize         = 0x28（40 B）
DataAlignment     = 4096
MetadataAlignment = 4
```

对齐值由 Header 声明而非协议写死。两者必须是 2 的幂，DataAlignment 至少为 4。其他介质可生成不同对齐的 RPKC1 包。

### 3.3 区域和重叠规则

- Entry 表必须完整位于 `[EntryOffset, HeaderSize - 4)`。
- Header CRC 位于 `[HeaderSize - 4, HeaderSize)`。
- 非空 Metadata 和 Data 必须位于 `[HeaderSize, PackSize)`。
- MetadataOffset 必须满足 MetadataAlignment。
- DataOffset 必须满足 DataAlignment。
- 任意两个非空 Metadata/Data 区间均不得重叠。
- 不允许两个 Entry 共享相同 Data，也不允许部分重叠去重。
- Metadata 为空时，其 Offset、Length 和 CRC 三个字段必须同时为零。
- 未使用 Entry 不写占位记录，由 EntryCount 决定有效项数量。

## 4. Header 和 Entry 字节布局

### 4.1 64 B Header 固定前缀

| Offset | Size | Field | 规则 |
| ---: | ---: | --- | --- |
| `0x00` | 4 | Magic | ASCII `RPKC` |
| `0x04` | 4 | FormatVersion | RPKC1 为 1 |
| `0x08` | 4 | HeaderSize | 当前为 `0x1000` |
| `0x0C` | 4 | PackSize | 完整包长度 |
| `0x10` | 4 | EntryOffset | 当前为 `0x40` |
| `0x14` | 2 | EntryCount | 有效 Entry 数量 |
| `0x16` | 2 | EntrySize | RPKC1 必须为 40 |
| `0x18` | 4 | DataAlignment | 当前 W25Q256 包为 4096 |
| `0x1C` | 4 | MetadataAlignment | 当前为 4 |
| `0x20` | 4 | VendorID | 由产品配置决定 |
| `0x24` | 4 | ProductID | 由产品配置决定 |
| `0x28` | 8 | PackageVersion | 单调递增 |
| `0x30` | 16 | Reserved | 必须全为 `0xFF` |

HeaderSize 由包声明，当前为 4 KiB。未来需要更多 Entry 时可以扩大 HeaderSize；Header CRC 始终位于 Header 最后 4 B，而不是在解析器中写死 `0xFFC`。

EntryCount 和 EntrySize 为 16 位。解析器必须检查：

```text
EntryOffset + EntryCount * EntrySize <= HeaderSize - 4
```

当前 4 KiB Header 和 40 B Entry 可容纳约 100 个资源。

### 4.2 40 B Entry

| Offset | Size | Field | 规则 |
| ---: | ---: | --- | --- |
| `0x00` | 4 | ResourceID | 非零、包内唯一、严格升序 |
| `0x04` | 2 | ResourceType | 见标准类型表 |
| `0x06` | 2 | Flags | RPKC1 必须为 0 |
| `0x08` | 8 | ResourceVersion | 同一 ID 的内容版本，单调递增 |
| `0x10` | 4 | DataOffset | 相对包首地址 |
| `0x14` | 4 | DataLength | 必须大于零 |
| `0x18` | 4 | DataCRC32 | 只覆盖 Data |
| `0x1C` | 4 | MetadataOffset | 相对包首地址；无 Metadata 时为 0 |
| `0x20` | 4 | MetadataLength | 无 Metadata 时为 0 |
| `0x24` | 4 | MetadataCRC32 | 无 Metadata 时为 0 |

ResourceID 是资源稳定身份，不使用 Entry 顺序或运行时名称查找。升级资源包时，同一语义资源保持 ID 不变，通过 ResourceVersion 表示内容变化。Component 打开包时拒绝重复或非升序 ID。

名称默认只存在于 PC 端 Manifest。若某种资源运行时确实需要名称，可在该类型 Metadata 中增加 UTF-8 名称；RPKC1 不建立公共字符串表。

Flags 字段为未来公共属性预留。首版不定义任何 Flag，非零即返回不支持。加载策略、Required、Preload、MemoryMapped 等均不得进入 Flags。

## 5. CRC32 和校验层级

全部 CRC 使用 CRC-32/ISO-HDLC：

```text
反射多项式  0xEDB88320
初始值      0xFFFFFFFF
输入反射    是
输出反射    是
最终异或    0xFFFFFFFF
```

它与 Python `binascii.crc32()` 以及当前 FTL 软件 CRC 风格一致。

- Header CRC 覆盖 `[0, HeaderSize - 4)`，包括 Entry 表和 `0xFF` Padding。
- Header CRC 自身位于 `[HeaderSize - 4, HeaderSize)`。
- Metadata CRC 只覆盖 Entry 指向的 Metadata。
- Data CRC 只覆盖 Entry 指向的 Data。
- 不设置整个 Pack CRC；Header、每项 Metadata 和每项 Data 已分别覆盖所有有意义内容，包体 Padding 不参与 CRC。
- CRC 保存于 Entry，不在 Metadata 内重复保存，避免重复字段和“CRC 是否覆盖自己”的特殊规则。

`ResourcePack_Open()` 只校验 Header、Entry 表、ID 顺序、范围、对齐、重叠和公共规则，不扫描所有大型 Data。资源使用前按需验证 Metadata/Data。

复制到 SDRAM 的资源采用：

```text
校验 Metadata
  -> memcpy(NOR Data, SDRAM)
  -> CRC32(SDRAM, DataLength) 与 Entry.DataCRC32 比较
  -> CortexM7DCache_Clean_Rounded(SDRAM, DataLength)
```

不先扫描一遍 NOR Data 再复制，目标副本 CRC 同时验证 NOR 内容、复制长度和 CPU 可见结果。原地读取的资源才直接对源 Data 计算 CRC。

## 6. 资源类型和兼容规则

### 6.1 顶层类型编号

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

CP936 的编码转换表本质是大数组，属于 BINARY，不属于 FONT。FONT 只表示可以直接提供 Glyph、点阵或矢量字形的资源。

### 6.2 子格式编号空间

所有 ImageFormat、PixelFormat、FontFormat、AudioCodec、ContainerFormat、ModelFormat、FirmwareImageFormat 等子格式字段统一使用 32 位编号：

```text
0x00000000              INVALID / UNKNOWN
0x00000001–0x7FFFFFFF   RPKC 标准定义
0x80000000–0xFFFFFFFE   用户或厂商自定义
0xFFFFFFFF              保留
```

### 6.3 Metadata 必选规则

| Type | Metadata |
| --- | --- |
| BINARY | 可选 |
| FONT | 必须 |
| IMAGE | 必须 |
| AUDIO | 必须 |
| MODEL | 必须 |
| FIRMWARE | 必须 |

未知或未启用类型不会阻止打开整个包。Core 仍校验其公共 Entry、范围和 CRC；调用未支持的类型 Metadata 解码器时返回 `UNSUPPORTED_TYPE`。Service 可以忽略不需要的未知资源；必需资源若类型未知、未启用或不兼容则初始化失败。

## 7. Metadata 公共前缀

所有非空 Metadata 以统一的 8 B 前缀开始：

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | MetadataVersion |
| `0x04` | 4 | MetadataSize |

MetadataSize 必须等于 Entry.MetadataLength。MetadataVersion 属于该资源类型自身的 Metadata 版本，不等于 RPKC FormatVersion。Component 必须先解码公共前缀，再调用类型解码器；未知版本返回不支持。

## 8. BINARY Metadata V1

BINARY Metadata 可选。存在时固定为 24 B：

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | MetadataVersion = 1 |
| `0x04` | 4 | MetadataSize = 24 |
| `0x08` | 4 | ElementFormat |
| `0x0C` | 4 | ByteOrder |
| `0x10` | 4 | ElementSize |
| `0x14` | 4 | ElementCount |

非 OPAQUE 数组必须使用 64 位中间值验证：

```text
ElementSize * ElementCount == DataLength
```

当前两张 CP936 表均为：

```text
ElementFormat = UINT16
ByteOrder     = LITTLE_ENDIAN
ElementSize   = 2
ElementCount  = 43586
DataLength    = 87172
```

完全不透明的 BINARY 可以不带 Metadata。

## 9. IMAGE Metadata V1

IMAGE 同时支持 PNG/JPEG 等编码图片和 RAW/LVGL 原始像素。Metadata V1 固定为 40 B：

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | MetadataVersion = 1 |
| `0x04` | 4 | MetadataSize = 40 |
| `0x08` | 4 | ImageFormat |
| `0x0C` | 4 | PixelFormat |
| `0x10` | 4 | Width |
| `0x14` | 4 | Height |
| `0x18` | 4 | StrideBytes |
| `0x1C` | 4 | FrameCount |
| `0x20` | 4 | ColorSpace |
| `0x24` | 4 | AlphaMode |

规则：

- Width、Height 和 FrameCount 必须大于零。
- RAW/LVGL 原始像素必须声明 PixelFormat 和 StrideBytes。
- PNG/JPEG 等编码图片允许 PixelFormat 和 StrideBytes 为零。
- FrameCount 为 1 表示静态图片；首版可以声明多帧，但不要求当前业务实现动画解码。

当前默认壁纸为：

```text
ImageFormat = LVGL_NATIVE
PixelFormat = TRUE_COLOR_ALPHA
Width       = 240
Height      = 320
StrideBytes = 720
FrameCount  = 1
DataLength  = 230400
```

Now Playing 唱片底图为：

```text
ResourceID  = 4
ImageFormat = LVGL_NATIVE
PixelFormat = TRUE_COLOR_ALPHA
Width       = 144
Height      = 144
StrideBytes = 432
FrameCount  = 1
DataLength  = 62208
```

像素为小端 RGB565 + 直通 Alpha（3 B/px）。源文件是 `Resources/imgs/` 下按文件名排序的 `.c`，忽略同目录 PNG；唱盘 C 只保留 `COLOR_DEPTH 16` 且无 16-bit swap 的一段。

## 10. FONT Metadata V1

FONT Payload 必须是可重定位的序列化格式，不允许直接保存含 MCU 指针、函数指针或编译器填充的 `lv_font_t`/C 运行时结构体镜像。

允许的 Payload 包括 TTF/OTF、自描述点阵字体和其他明确声明为可重定位的字体格式。加载后由字体业务构造运行时对象。

FONT Metadata V1 使用至少 32 B 的公共部分：

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | MetadataVersion = 1 |
| `0x04` | 4 | MetadataSize，至少为 32 |
| `0x08` | 4 | FontFormat |
| `0x0C` | 4 | CodepointEncoding |
| `0x10` | 4 | GlyphCount |
| `0x14` | 4 | NominalPixelHeight，矢量字体可为 0 |
| `0x18` | 4 | Ascent，int32_t |
| `0x1C` | 4 | Descent，int32_t |
| `0x20` | 可变 | 格式专用尾部 |

Component 解码公共字段并返回尾部 View，不把 LVGL 私有布局写入 Common Metadata。

## 11. AUDIO Metadata V1

AUDIO 同时支持容器音频、压缩码流和原始 PCM。容器与 Codec 分开描述。Metadata V1 固定为 48 B：

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | MetadataVersion = 1 |
| `0x04` | 4 | MetadataSize = 48 |
| `0x08` | 4 | ContainerFormat |
| `0x0C` | 4 | Codec |
| `0x10` | 4 | SampleRate |
| `0x14` | 4 | ChannelCount |
| `0x18` | 4 | BitsPerSample |
| `0x1C` | 4 | SamplesPerFrame |
| `0x20` | 8 | TotalSamples |
| `0x28` | 4 | NominalBitrate |
| `0x2C` | 4 | ChannelLayout |

SampleRate 和 ChannelCount 必须大于零。不适用或未知字段填零。时长由 TotalSamples 和 SampleRate 计算，避免使用精度较低的 DurationMs。

## 12. MODEL Metadata V1

MODEL Metadata 使用 40 B 公共头加可变 Tensor 表：

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | MetadataVersion = 1 |
| `0x04` | 4 | MetadataSize，至少为 40 |
| `0x08` | 4 | ModelFormat |
| `0x0C` | 4 | ModelFormatVersion |
| `0x10` | 4 | RequiredWorkMemory |
| `0x14` | 4 | RequiredPersistentMemory |
| `0x18` | 4 | RequiredAlignment |
| `0x1C` | 2 | InputCount |
| `0x1E` | 2 | OutputCount |
| `0x20` | 4 | TensorTableOffset，相对 Metadata 起点 |
| `0x24` | 4 | TensorTableLength |
| `0x28` | 可变 | Tensor 表或格式专用区域 |

RequiredAlignment 必须为 2 的幂。Tensor 表必须完整位于 Metadata 内。Component 不分配模型工作区；模型 Service 根据 Metadata 判断当前 SDRAM 是否满足要求。

### 12.1 Tensor Descriptor V1

每条 Tensor 使用 32 B 固定头加可变区域：

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | DescriptorSize |
| `0x04` | 2 | TensorIndex |
| `0x06` | 2 | Direction，INPUT/OUTPUT |
| `0x08` | 4 | DataType |
| `0x0C` | 4 | QuantizationType |
| `0x10` | 4 | Rank |
| `0x14` | 4 | DimensionsOffset，相对 Descriptor 起点 |
| `0x18` | 4 | QuantizationOffset，相对 Descriptor 起点 |
| `0x1C` | 4 | QuantizationLength |
| `0x20` | 可变 | Rank 个 uint32_t 维度和可选量化参数 |

无量化参数时 QuantizationType 为 NONE，Offset 和 Length 同时为零。DescriptorSize 用于安全跳转到下一条记录，允许不同 Rank 和以后扩展量化数据。

## 13. FIRMWARE Metadata V1

FIRMWARE 只作为独立 Firmware Update Service 的升级载荷。Resource Service 可以枚举和校验，但不得自动写入内部 Flash、外设或跳转执行。

首版 Payload 只支持可直接写入目标 Slot 的 RAW_BINARY，不支持 ELF、HEX 或 SREC。以后若需要差分升级，增加明确的 DELTA_PATCH ImageFormat。

所有固件必须携带 SHA-256。RPKC1 支持数字签名；Debug 配置可以允许未签名固件，正式发布必须拒绝未签名或签名错误的固件。

### 13.1 固定头

FIRMWARE Metadata V1 使用 120 B 固定头，签名位于尾部：

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | MetadataVersion = 1 |
| `0x04` | 4 | MetadataSize |
| `0x08` | 4 | TargetType |
| `0x0C` | 4 | TargetID |
| `0x10` | 4 | HardwareRevisionMin |
| `0x14` | 4 | HardwareRevisionMax |
| `0x18` | 4 | VersionMajor |
| `0x1C` | 4 | VersionMinor |
| `0x20` | 4 | VersionPatch |
| `0x24` | 4 | VersionBuild |
| `0x28` | 8 | SecurityVersion，单调递增 |
| `0x30` | 4 | ImageFormat，V1 为 RAW_BINARY |
| `0x34` | 4 | LoadAddress |
| `0x38` | 4 | EntryAddress |
| `0x3C` | 4 | Reserved，必须为 `0xFFFFFFFF` |
| `0x40` | 32 | DataSHA256 |
| `0x60` | 4 | SignatureAlgorithm |
| `0x64` | 4 | SignatureEncoding |
| `0x68` | 4 | KeyID |
| `0x6C` | 4 | SignatureOffset，相对 Metadata 起点 |
| `0x70` | 4 | SignatureLength |
| `0x74` | 4 | Reserved，必须为 `0xFFFFFFFF` |
| `0x78` | 可变 | Padding 和签名区域 |

当前算法为：

```text
SignatureAlgorithm = ECDSA_P256_SHA256
SignatureEncoding  = RAW_RS
SignatureOffset    = 0x80
SignatureLength    = 64
MetadataSize       = 0xC0
```

RAW_RS 前 32 B 为大端 r，后 32 B 为大端 s。KeyID 用于选择设备内置公钥。

### 13.2 签名覆盖范围

签名必须同时绑定固件数据摘要和关键使用 Metadata，不能只签 Data。否则攻击者可能在不改变固件字节的情况下修改 TargetID、硬件版本、LoadAddress 或 EntryAddress，并重新计算普通 CRC。

签名输入按以下固定顺序规范化编码：

```text
ASCII "RPKC1-FIRMWARE"（不含 NUL）
VendorID
ProductID
ResourceID
ResourceType
ResourceVersion
TargetType
TargetID
HardwareRevisionMin
HardwareRevisionMax
VersionMajor
VersionMinor
VersionPatch
VersionBuild
SecurityVersion
ImageFormat
LoadAddress
EntryAddress
DataLength
DataSHA256
SignatureAlgorithm
SignatureEncoding
KeyID
```

整数使用固定小端编码，DataSHA256 按原始 32 B 放入。对 CanonicalBytes 计算 SHA-256，再使用 ECDSA P-256 签名摘要。DataOffset、MetadataOffset、CRC、Padding 和 PackageVersion 不参与签名，使同一份签名固件可以在相同 Vendor/Product 的不同资源包版本中重新排列而无需重新签名。
