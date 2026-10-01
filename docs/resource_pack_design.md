# RPKC1 通用资源包设计

> 状态：首版 PC 打包器、ResourcePack Core/BINARY/IMAGE、Resource Service 和 APP 启动接线已实现；主机协议测试与 Debug 固件构建通过，等待实际打包、重新烧录和硬件启动验收。
> 日期：2026-09-01。
> 首版范围：实现 Core、BINARY 与 IMAGE；FONT、AUDIO、MODEL、FIRMWARE 先完成协议定义，类型解码器默认关闭。
> 当前硬件：STM32H743ZG、32 MiB W25Q256、32 MiB SDRAM。
> 当前资源：CP936 的 `uni2oem`、`oem2uni` 两张表、默认壁纸，以及 Now Playing 唱片底图 IMAGE（PC 烧录进 Pack；不进内部 Flash）。固件启动加载 ID 1–4；Canvas 用 ID 4 底图叠假封面后绑到 Now Playing 唱盘 Image。

## 1. 目标与边界

RPKC1 是与具体 MCU、Flash 和业务无关的只读资源容器格式。它负责描述资源身份、类型、版本、数据范围、类型元数据和完整性校验，不决定资源如何加载或由谁使用。

首版明确遵守以下边界：

- Resource Type 只描述“内容是什么”，不描述加载方式。
- 加载到 SDRAM、原地读取、启动必需、可选资源等策略由使用方决定。
- ResourcePack Component 只解析一段内存并执行结构、CRC 和类型 Metadata 校验，不认识 W25Qxx、QSPI、Platform、Service、FatFs、LVGL 或 FreeRTOS。
- Resource Service 负责当前产品的包位置、产品身份、必需资源、目标地址、复制、日志和失败策略。
- Platform Flash 负责 W25Q256 装配和 QSPI 内存映射生命周期。
- PC 端资源提取和打包继续位于 `Tools/package_maker/`，不属于固件 Component。
- 首版资源包是不可变完整镜像，不支持设备运行时原地更新单个资源。设备侧整包更新的产品路径已由 [ADR-0015](adr/0015-volume-roles-and-resource-install.md) 约定：SD 上的安装包先完整落入 FTL 暂存并校验，再写入本 Pack 物理区；不得从 SD 流式编程。该路径尚未实现。
- 生效中的壁纸、模型与 Now Playing 唱片底图只认机内 Pack，不把 SD 或 FTL 上的普通文件当作长期资源源。唱片底图作为 ResourceID 4 入包，启动时加载到 SDRAM 唱盘槽。
- 首版不提供公共透明压缩或加密。对应能力以后通过新协议能力或外层机制增加。

当前旧格式 `RPK1` 是固定三资源的临时格式。RPKC1 实现后必须重新生成并烧录外部资源包；旧包不与 RPKC1 兼容。

## 2. 分层和运行链路

### 2.1 Module 职责

| Module | 职责 | 不承担 |
| --- | --- | --- |
| `Tools/package_maker` | 提取项目资源、读取 JSON、生成 RPKC1 和外部清单 | 固件运行时解析 |
| ResourcePack Component | 通用包头、Entry、Metadata 解码和校验 | W25Qxx、目标地址、日志、业务失败策略 |
| Platform Flash | 初始化 W25Q256、开启只读映射、管理映射和间接请求互斥 | RPKC1 字段解释 |
| Resource Service | 当前包配置、目标表、加载、目标 CRC、Cache Clean、日志 | QSPI/HAL 细节、类型协议算法 |
| APP | 初始化顺序和致命错误策略 | 解析资源包或直接访问 NOR |

### 2.2 启动链路

```text
Platform_Log_Init()
  -> Platform_Init()
       -> Platform_SDRAM_Init()
       -> Platform_Flash_Init()
  -> Service_Resource_Init()
       -> Platform_Flash_EnableMemoryMappedMode()
       -> mapped pack = mapped_base + FlashOffset
       -> ResourcePack_Open()
       -> 按 Service 目标表查找资源
       -> 校验 Metadata 和当前业务约束
       -> memcpy NOR 映射数据到 SDRAM
       -> 对 SDRAM 副本计算 Data CRC32
       -> CortexM7DCache_Clean_Rounded()
       -> 标记 READY
  -> app_task_start()
       -> GUI / Storage 等任务开始使用资源
```

首版 Handle 只在 `Service_Resource_Init()` 中使用。Service 不向其他 Module 发布 NOR 映射指针，也不在任务启动后继续访问 Entry View。后续 FTL 间接操作可能暂时退出 QSPI 映射，因此未来的按需音频、模型或固件读取必须建立由 Storage Task 串行化的读取接口，不能长期持有 `0x90000000` 窗口指针。

Resource Service 可以直接调用无状态的 Cortex-M7 Cache Adapter。它不需要板级实例或 HAL Handle，与必须由 Platform 装配的具体外设 Adapter 不同。当前复制链路只需 `CortexM7DCache_Clean_Rounded()`；CPU 写入、CRC 和后续消费者均由 CPU 执行，不需要 Invalidate。

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

## 14. Component 设计

### 14.1 零动态内存

Component 不使用 malloc，也不复制整个 Entry 表。Handle 只保存：

- 包内存首地址；
- 调用者提供的可用窗口长度；
- 已解码 Header；
- Open 状态。

每次 Get/Find 直接从映射窗口显式解码一个 40 B Entry。Entry 表已按 ID 排序，因此 Find 可以使用二分查找。

建议的 Core Interface：

```c
ResourcePack_StatusTypeDef ResourcePack_Open(
    const uint8_t *pack,
    uint32_t available_size,
    ResourcePack_HandleTypeDef *handle);

ResourcePack_StatusTypeDef ResourcePack_GetEntryCount(
    const ResourcePack_HandleTypeDef *handle,
    uint16_t *entry_count);

ResourcePack_StatusTypeDef ResourcePack_GetEntry(
    const ResourcePack_HandleTypeDef *handle,
    uint16_t index,
    ResourcePack_EntryTypeDef *entry);

ResourcePack_StatusTypeDef ResourcePack_FindEntry(
    const ResourcePack_HandleTypeDef *handle,
    uint32_t resource_id,
    ResourcePack_EntryTypeDef *entry);

ResourcePack_StatusTypeDef ResourcePack_GetEntryView(
    const ResourcePack_HandleTypeDef *handle,
    const ResourcePack_EntryTypeDef *entry,
    ResourcePack_EntryViewTypeDef *view);

ResourcePack_StatusTypeDef ResourcePack_VerifyMetadata(...);
ResourcePack_StatusTypeDef ResourcePack_VerifyBuffer(...);
```

类型 Metadata 解析由 Component 完成，业务适配由 Service 完成。例如 Component 解码 Width、Height、PixelFormat；Service 判断当前默认壁纸必须是 240 × 320 且与 LVGL 配置兼容。

### 14.2 类型编译开关

首版配置：

```c
#define RESOURCE_PACK_TYPE_BINARY_ENABLE    1
#define RESOURCE_PACK_TYPE_FONT_ENABLE      0
#define RESOURCE_PACK_TYPE_IMAGE_ENABLE     1
#define RESOURCE_PACK_TYPE_AUDIO_ENABLE     0
#define RESOURCE_PACK_TYPE_MODEL_ENABLE     0
#define RESOURCE_PACK_TYPE_FIRMWARE_ENABLE  0
```

开关只裁剪类型专用 Metadata 解码器，不删除标准 Type 编号，也不影响 Core 对公共 Entry、范围和 CRC 的识别。关闭类型的解码调用返回 `RESOURCE_PACK_UNSUPPORTED_TYPE`。

建议目录：

```text
Components/resource_pack/
  resource_pack.c
  resource_pack.h
  resource_pack_config.h
  resource_pack_binary.c
  resource_pack_image.c
  resource_pack_font.c
  resource_pack_audio.c
  resource_pack_model.c
  resource_pack_firmware.c
```

首版只需实现 Core、BINARY 和 IMAGE；其余协议先保留在本文档，专用解析代码以后按实际资源加入。

## 15. Resource Service 设计

### 15.1 Source Config

当前资源来源由单个静态配置对象描述，不把字段分散为多个宏：

```c
static const Service_ResourceSourceConfigTypeDef resource_source_config =
{
    .FlashOffset = 0x00401000UL,
    .MaximumSize = 0x003FE000UL,
    .VendorID = /* 当前厂商 ID */,
    .ProductID = /* 当前产品 ID */
};
```

当前 24 MiB FTL 从 `0x007FF000` 开始，因此资源空间为：

```text
W25Q256 物理范围：0x00401000–0x007FEFFF
映射范围：        0x90401000–0x907FEFFF
最大长度：        0x003FE000
```

Service 必须先验证 FlashOffset、MaximumSize 和 Platform 返回的映射窗口，再把包首地址交给 Component。包声明的 PackSize 不得越过 MaximumSize 或映射窗口。

### 15.2 Target Config

加载目标使用静态结构体数组。每个目标至少声明：

```c
typedef struct
{
    uint32_t ResourceID;
    uint16_t ExpectedType;
    uint8_t *DestinationStart;
    uint8_t *DestinationEnd;
    bool Required;
    bool VerifyAfterCopy;
} Service_ResourceTargetTypeDef;
```

目标数量通过 `sizeof(array) / sizeof(array[0])` 计算，不使用写死的资源数量宏。Service 查找时同时核对 ResourceID 和 ExpectedType，防止相同 ID 被错误类型替换。

当前链接器已预留：

| 资源 | SDRAM 起点 | 长度 |
| --- | ---: | ---: |
| CP936 `uni2oem` | `0xC0000000` | `0x15484` |
| CP936 `oem2uni` | `0xC00154A0` | `0x15484` |
| 默认壁纸 Data | `0xC002A940` | `0x38400` |

默认壁纸 Data 由 `Service/gui/view/gui_service_view_wallpaper_region.c` 的像素数组占位（输入段名沿用 `.rodata.ui_img_wallpaper_indigo_mist_soft_dark_png_data`），GUI 经 `view/` 的壁纸描述符读取；模拟器从 `Resources/imgs/` 的打包源装入同一数组。

三个目标首地址均按 32 B 对齐；两个 CP936 表后的 28 B Padding 允许 `Clean_Rounded()` 安全覆盖最后一条 Cache Line。

### 15.3 状态、失败和日志

- Header、VendorID 或 ProductID 不合法：整个 Service 初始化失败。
- 必需资源缺失、类型错误、类型未启用、Metadata 不兼容、CRC 错误或目标空间不足：初始化失败，APP 记录诊断后进入 `Error_Handler()`。
- 可选资源缺失或不兼容：记录警告并继续。
- Component 不依赖日志；Service 统一输出。
- Service 保存最后失败 ResourceID、阶段和 Component 状态，供 APP 查询。

日志开关：

```c
#define SERVICE_RESOURCE_LOG_ENABLE          1
#define SERVICE_RESOURCE_LOG_ENTRY_ENABLE    0
#define SERVICE_RESOURCE_LOG_TIMING_ENABLE   0
```

基础日志默认输出 VendorID、ProductID、PackageVersion 和 EntryCount；详细 Entry 枚举与加载耗时默认关闭。

## 16. PC 工具设计

### 16.1 目录和职责

```text
Tools/package_maker/
  extract_project_resources.py
  rpkc_pack.py
  resource_pack.json
  main.py
```

- `extract_project_resources.py`：项目专用。BINARY 仍从 FatFs `cc936.c` 抽取；IMAGE 扫描 `Resources/imgs/*.c`（按文件名排序，忽略 PNG），从每个文件的第一个 `uint8_t` 数组和 `lv_img_dsc_t` 抽出 `.bin` 与宽高。
- `rpkc_pack.py`：通用 RPKC1 打包器，不认识 FatFs、LVGL C 数组或当前文件路径。
- `resource_pack.json`：只声明 BINARY 身份、类型、版本和输入文件；IMAGE 由 `main.py` 按扫描结果追加。
- `main.py`：依次执行提取、追加 IMAGE、组包，并打印打包顺序。

以后直接打包 MP3、模型或固件文件时，不需要增加 C 数组提取逻辑。

### 16.2 JSON 示例

```json
{
  "vendor_id": 1,
  "product_id": 1,
  "package_version": 2,
  "header_size": 4096,
  "data_alignment": 4096,
  "metadata_alignment": 4,
  "resources": [
    {
      "id": 1,
      "type": "BINARY",
      "version": 1,
      "data": "generated/uni2oem.bin",
      "metadata": {
        "element_format": "UINT16",
        "byte_order": "LITTLE_ENDIAN",
        "element_size": 2,
        "element_count": 43586
      }
    }
  ]
}
```

打包器负责：

1. 校验 JSON、ID 唯一性和字段范围。
2. 按 ResourceID 排序。
3. 编码类型 Metadata。
4. 根据 Header 声明的对齐生成 Offset。
5. 填充 `0xFF`。
6. 计算 Metadata、Data 和 Header CRC32。
7. 输出 `resource_pack.bin`。
8. 输出包含名称、来源、ID、Type、Version、Offset、Length 和 CRC 的 JSON Manifest。

运行时名称默认不进入二进制包；完整名称与来源由外部 Manifest 保存。

## 17. 首版实施范围

首版实现并启用：

- RPKC1 Core Header/Entry 解码；
- Header CRC、范围、对齐、排序、重复和重叠校验；
- BINARY Metadata V1；
- IMAGE Metadata V1；
- 当前四个资源的 JSON、扫描提取和组包；
- Resource Service 启动期一次性加载 ID 1–4；
- Vendor/Product/PackageVersion 日志；
- SDRAM 目标 CRC 和 Cache Clean。

当前 Pack（`package_version` 2）打包：

- BINARY ResourceID 1：CP936 `uni2oem`；
- BINARY ResourceID 2：CP936 `oem2uni`；
- IMAGE ResourceID 3：默认 240 × 320 LVGL TRUE_COLOR_ALPHA 壁纸；
- IMAGE ResourceID 4：Now Playing 唱片底图 144 × 144 LVGL TRUE_COLOR_ALPHA。由 PC 打包器写入 Pack 并烧录；不把该 PNG 编进内部 Flash。固件把 ID 4 拷到 SDRAM 唱盘槽，Canvas 叠假封面后绑到 Now Playing 唱盘 Image。真 ID3 封面不在本条。

首版不实现：

- FONT/AUDIO/MODEL/FIRMWARE 类型专用解码器；
- 通用压缩、加密；
- 单资源原地更新；
- 资源包 A/B 或掉电提交协议；
- 运行期 NOR View；
- 按需音频或模型流式读取；
- 固件写入、验签执行和 Bootloader 跳转。

FONT、AUDIO、MODEL 和 FIRMWARE 的格式已经在本文定义。以后实际启用时补齐对应解码器、编号表、测试和使用 Service，不修改已确认的 RPKC1 Core 布局。

## 18. 实施与验收检查

### 18.1 打包器

- 相同输入重复执行生成完全相同的 `resource_pack.bin`。
- Entry 按 ID 严格升序。
- Header/Entry 字段偏移与本文一致。
- 所有整数为小端。
- Header、Metadata 和 Data CRC 与独立工具结果一致。
- Padding 为 `0xFF`。
- 包不越过 `0x003FE000` 资源空间。

### 18.2 Component

- 拒绝错误 Magic、Version、HeaderSize、EntrySize 和 Header CRC。
- 拒绝 ID 为零、重复或非升序。
- 拒绝整数溢出、越界、错误对齐和任意区域重叠。
- 允许合法未知 Type 打开，禁用解码器返回 `UNSUPPORTED_TYPE`。
- 不使用动态内存，不直接转换落盘结构体。
- BINARY 和 IMAGE Metadata 分别覆盖合法和非法边界。

### 18.3 固件集成

- `Service_Resource_Init()` 只在 Platform SDRAM/Flash 成功后调用。
- 当前四个链接器符号仍位于 SDRAM NOLOAD 段。
- 复制前核对目标容量，复制后核对 SDRAM CRC。
- Cache Clean 范围满足 32 B 首地址对齐和归属约束。
- 任何失败均不得启动使用未初始化 CP936、壁纸或唱盘底图的任务。
- 初始化结束后不保留或发布 NOR 映射 View。

### 18.4 烧录

RPKC1 新包仍烧录到 CubeProgrammer 映射地址 `0x90401000`，对应 W25Q256 物理偏移 `0x00401000`。必须使用已经验证的 `W25Q256_STM32H743ZG` External Loader，并启用写后校验。新包生成后重新核对实际 PackSize 和结束地址，不能继续沿用旧 RPK1 的 `0x65000` 长度。
