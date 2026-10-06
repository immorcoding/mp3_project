# Resources · 扩展类型格式

[返回资源规则](resources.md) · [Core 与当前类型](resources.format.md)。以下 FONT/AUDIO/MODEL/FIRMWARE 是协议预留，当前类型解码器未启用；格式声明不表示加载、验签或执行能力已实现。

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
