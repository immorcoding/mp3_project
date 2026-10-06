# Resources · 加载与打包

[返回资源规则](resources.md)。本参考记录产品部署与资源生命周期；字段与 CRC 唯一正文在 [RPKC1 格式](resources.format.md)，公开 API 见 [Component](../../Components/resource_pack/README.md)和 [Service](../../Service/resource/README.md)。

## 当前能力与资源

Core、BINARY、IMAGE、PC 打包器及启动接线已实现；FONT/AUDIO/MODEL/FIRMWARE 只有协议预留。尚无运行期 NOR View、流式资源、单资源更新、A/B 掉电提交、通用压缩/加密或固件执行/验签业务。设备安装路径已决策但尚未实现（RES-2）。

| ResourceID | 内容 | 当前部署 |
| ---: | --- | --- |
| 1 | CP936 uni2oem | UINT16 小端表，SDRAM NOLOAD |
| 2 | CP936 oem2uni | UINT16 小端表，SDRAM NOLOAD |
| 3 | 默认壁纸 | 240×320 LVGL TRUE_COLOR_ALPHA，230400 B |
| 4 | Now Playing 唱片底图 | 144×144 同格式，62208 B，SDRAM 唱盘槽 |

CP936 是编码转换数组，不是 FONT。ID 是稳定语义身份，增加/排序图片时核对 ID 分配，不以 Entry 位置识别资源。图片像素为小端 RGB565 + straight Alpha（3 B/px）。壁纸描述符指向预留数组，模拟器从打包源填充；vinyl 从唱盘槽构造描述符并叠假封面，当前不读取 ID3。图片像素不编入内部 Flash。

链接器目标地址/容量与产品身份、ID、日志开关分别以链接脚本和 `Service/resource/resource_service_config.h` 为准，不维护第二份结构体/地址表。所有目标须位于 SDRAM NOLOAD，Cache Clean 向外取整区域也须归本目标所有；CP936 尾部 Padding 用于容纳最后一条 32 B Cache Line。

## 启动与失败

```text
Platform_Log_Init → Platform_Init（SDRAM、Flash 成功）
 → Service_Resource_Init
 → EnableMemoryMappedMode → 映射基址 + FlashOffset
 → Open → 按目标表查找 ID / ExpectedType → 验证 Metadata 与业务约束
 → 核对目标容量 → 复制至 SDRAM → 校验目标 CRC → Clean_Rounded
 → 全部必需目标 READY → app_task_start
```

当前四项均为必需。Header/Vendor/Product 不合法，或必需资源缺失、类型/Metadata/CRC 不兼容、目标不足、Cache Clean 失败，都阻止消费者启动，由 APP 记录诊断并进入 Error_Handler。可选目标才允许警告后继续；Service 保留最近失败 ID、阶段、Component 状态并统一日志，Component 不记录产品日志。

SourceConfig 的 FlashOffset/MaximumSize 先对 Platform 映射窗口校验，随后 PackSize 同时受窗口与产品资源区约束。当前 NOR 物理资源区为 `0x00401000–0x007FEFFF`，最大 `0x003FE000`，相应映射区 `0x90401000–0x907FEFFF`；FTL 自 `0x007FF000` 起，包不得侵入。

Handle/View 仅在 Init 有效，不向其他模块发布。复制后直接对 SDRAM 计算 Data CRC，不提前再扫一次 NOR；该校验覆盖内容、复制长度及 CPU 可见结果。CPU 写入/CRC/消费链只需 Clean，不加 Invalidate；无状态 Cortex-M7 Adapter 可由 Service 调用，无须板级外设装配。未来原地读取才校验源 Data，并另行建立 RES-3 的运行期串行接口。

## PC 构建与烧录

[package_maker](../../Tools/package_maker/main.py)依次提取项目资源、追加 IMAGE、组包；通用 `rpkc_pack.py` 不解释 FatFs/LVGL 源文件。CP936 来自 cc936.c；IMAGE 扫描 `Resources/imgs/*.c` 按文件名排序，忽略 PNG，从首个 uint8_t 数组与 lv_img_dsc_t 提取。唱盘 C 只保留 COLOR_DEPTH 16 且不交换 16-bit 字节的一段。JSON 实例由工具配置维护，新增普通音频/模型文件无需增加 C 数组提取。

相同输入必须生成相同二进制：校验字段/唯一 ID、按 ID 排序、编码 Metadata、按 Header 对齐、以 0xFF 填充，再写各级 CRC。外部 JSON Manifest 保存名称、来源、ID、类型、版本、范围与 CRC；运行时包不建公共名称表。字段偏移和 CRC 用独立已知结果核对，不用打包器自证自身。

使用已验证的 W25Q256_STM32H743ZG External Loader，把包烧录到 CubeProgrammer 地址 `0x90401000` 并启用写后校验；每次重新核对实际 PackSize/结束地址。RPKC1 不兼容历史固定三资源 RPK1，需要重新生成烧录，不能复用其旧长度 `0x65000`。

## 验证边界

解码器覆盖 Magic/Version/尺寸/CRC、零或重复或无序 ID、64 位范围溢出、越界、对齐及任意重叠；未知类型公共合法时允许 Open，类型解码返回 UNSUPPORTED_TYPE。BINARY/IMAGE 的合法和非法 Metadata 均需覆盖。固件验证四个 NOLOAD 槽、容量/目标 CRC、32 B Cache 所有权及失败阻止启动；软件结果和烧录回读不代替板级验收，板上状态留 tracker。
