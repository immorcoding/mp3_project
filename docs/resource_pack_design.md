# RPKC1 通用资源包设计

> 能力范围：PC 打包器、ResourcePack Core/BINARY/IMAGE、Resource Service 和 APP 启动接线已实现。测试、烧录和板级验收结果在对应 GitHub ticket/置顶「板上状态」记录。
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

## 二进制兼容性契约

Header/Entry 字段、CRC、范围/重叠规则、类型编号和各 Metadata V1 的唯一正文在 [RPKC1 格式](resource_pack_format.md)。修改打包器、解码器或包版本时读取它；只改启动加载时无需加载所有保留类型的字段表。

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

当前实现 Core、BINARY 和 IMAGE；其余协议保留在 [格式文档](resource_pack_format.md)，专用解析代码按实际资源需求加入。

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

FONT、AUDIO、MODEL 和 FIRMWARE 的格式已经在 [格式文档](resource_pack_format.md) 定义。以后实际启用时补齐对应解码器、编号表、测试和使用 Service，不修改已确认的 RPKC1 Core 布局。

## 18. 实施与验收检查

### 18.1 打包器

- 相同输入重复执行生成完全相同的 `resource_pack.bin`。
- Entry 按 ID 严格升序。
- Header/Entry 字段偏移与格式文档一致。
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
