# RPKC1 扩展载荷数据字典

[Resources](../resources.md) 的预留兼容契约，当前解码器未启用；固件执行与发布认证策略见 RES-8，公共编码见[容器字典](rpkc1-layout.md)。Offset 相对所属结构起点，显式说明相对基准的字段除外。

## 载荷约束

| 类型 | 固定区与附加约束 |
| --- | --- |
| FONT V1 | 至少32 B；载荷为可重定位序列化字体（如TTF/OTF/自描述点阵），不含MCU指针、函数指针或编译器结构填充；其余为格式专用尾部。 |
| AUDIO V1 | 48 B；容器与Codec独立；SampleRate/ChannelCount > 0；未知或不适用字段为0；时长由TotalSamples/SampleRate推导，无DurationMs字段。 |
| MODEL V1 | 至少40 B加Tensor表；RequiredAlignment为2的幂；Tensor表完整位于Metadata内。 |
| Tensor V1 | 32 B固定头加Rank个uint32维度及量化数据；DescriptorSize覆盖整条记录；无量化时类型为NONE且量化Offset/Length同时为0。 |
| FIRMWARE V1 | 120 B固定头加签名区；载荷仅RAW_BINARY，不包含ELF/HEX/SREC；携带SHA-256。 |
| 签名编码 | ECDSA_P256_SHA256、RAW_RS；SignatureOffset=80h，SignatureLength=64，MetadataSize=C0h；RAW_RS是32 B大端r接32 B大端s，KeyID选设备公钥。 |

## 字段字典

| 结构 | Offset | Size | Field |
| --- | ---: | ---: | --- |
| FONT V1 | `0x00` | 4 | MetadataVersion = 1 |
| FONT V1 | `0x04` | 4 | MetadataSize，至少为 32 |
| FONT V1 | `0x08` | 4 | FontFormat |
| FONT V1 | `0x0C` | 4 | CodepointEncoding |
| FONT V1 | `0x10` | 4 | GlyphCount |
| FONT V1 | `0x14` | 4 | NominalPixelHeight，矢量字体可为 0 |
| FONT V1 | `0x18` | 4 | Ascent，int32_t |
| FONT V1 | `0x1C` | 4 | Descent，int32_t |
| FONT V1 | `0x20` | 可变 | 格式专用尾部 |
| AUDIO V1 | `0x00` | 4 | MetadataVersion = 1 |
| AUDIO V1 | `0x04` | 4 | MetadataSize = 48 |
| AUDIO V1 | `0x08` | 4 | ContainerFormat |
| AUDIO V1 | `0x0C` | 4 | Codec |
| AUDIO V1 | `0x10` | 4 | SampleRate |
| AUDIO V1 | `0x14` | 4 | ChannelCount |
| AUDIO V1 | `0x18` | 4 | BitsPerSample |
| AUDIO V1 | `0x1C` | 4 | SamplesPerFrame |
| AUDIO V1 | `0x20` | 8 | TotalSamples |
| AUDIO V1 | `0x28` | 4 | NominalBitrate |
| AUDIO V1 | `0x2C` | 4 | ChannelLayout |
| MODEL V1 | `0x00` | 4 | MetadataVersion = 1 |
| MODEL V1 | `0x04` | 4 | MetadataSize，至少为 40 |
| MODEL V1 | `0x08` | 4 | ModelFormat |
| MODEL V1 | `0x0C` | 4 | ModelFormatVersion |
| MODEL V1 | `0x10` | 4 | RequiredWorkMemory |
| MODEL V1 | `0x14` | 4 | RequiredPersistentMemory |
| MODEL V1 | `0x18` | 4 | RequiredAlignment |
| MODEL V1 | `0x1C` | 2 | InputCount |
| MODEL V1 | `0x1E` | 2 | OutputCount |
| MODEL V1 | `0x20` | 4 | TensorTableOffset，相对 Metadata 起点 |
| MODEL V1 | `0x24` | 4 | TensorTableLength |
| MODEL V1 | `0x28` | 可变 | Tensor 表或格式专用区域 |
| Tensor V1 | `0x00` | 4 | DescriptorSize |
| Tensor V1 | `0x04` | 2 | TensorIndex |
| Tensor V1 | `0x06` | 2 | Direction，INPUT/OUTPUT |
| Tensor V1 | `0x08` | 4 | DataType |
| Tensor V1 | `0x0C` | 4 | QuantizationType |
| Tensor V1 | `0x10` | 4 | Rank |
| Tensor V1 | `0x14` | 4 | DimensionsOffset，相对 Descriptor 起点 |
| Tensor V1 | `0x18` | 4 | QuantizationOffset，相对 Descriptor 起点 |
| Tensor V1 | `0x1C` | 4 | QuantizationLength |
| Tensor V1 | `0x20` | 可变 | Rank 个 uint32_t 维度和可选量化参数 |
| FIRMWARE V1 | `0x00` | 4 | MetadataVersion = 1 |
| FIRMWARE V1 | `0x04` | 4 | MetadataSize |
| FIRMWARE V1 | `0x08` | 4 | TargetType |
| FIRMWARE V1 | `0x0C` | 4 | TargetID |
| FIRMWARE V1 | `0x10` | 4 | HardwareRevisionMin |
| FIRMWARE V1 | `0x14` | 4 | HardwareRevisionMax |
| FIRMWARE V1 | `0x18` | 4 | VersionMajor |
| FIRMWARE V1 | `0x1C` | 4 | VersionMinor |
| FIRMWARE V1 | `0x20` | 4 | VersionPatch |
| FIRMWARE V1 | `0x24` | 4 | VersionBuild |
| FIRMWARE V1 | `0x28` | 8 | SecurityVersion，单调递增 |
| FIRMWARE V1 | `0x30` | 4 | ImageFormat，V1 为 RAW_BINARY |
| FIRMWARE V1 | `0x34` | 4 | LoadAddress |
| FIRMWARE V1 | `0x38` | 4 | EntryAddress |
| FIRMWARE V1 | `0x3C` | 4 | Reserved，必须为 `0xFFFFFFFF` |
| FIRMWARE V1 | `0x40` | 32 | DataSHA256 |
| FIRMWARE V1 | `0x60` | 4 | SignatureAlgorithm |
| FIRMWARE V1 | `0x64` | 4 | SignatureEncoding |
| FIRMWARE V1 | `0x68` | 4 | KeyID |
| FIRMWARE V1 | `0x6C` | 4 | SignatureOffset，相对 Metadata 起点 |
| FIRMWARE V1 | `0x70` | 4 | SignatureLength |
| FIRMWARE V1 | `0x74` | 4 | Reserved，必须为 `0xFFFFFFFF` |
| FIRMWARE V1 | `0x78` | 可变 | Padding 和签名区域 |

## 签名输入

按下面顺序拼接 CanonicalBytes：整数采用字段固定宽度的小端编码，DataSHA256 原样32 B，标识字符串不含NUL。对拼接结果计算SHA-256，再以ECDSA P-256签该摘要。

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

DataOffset、MetadataOffset、CRC、Padding、PackageVersion 不参与签名；同一 Vendor/Product 内可重排同份签名资源。签名同时绑定载荷摘要与使用元数据。
