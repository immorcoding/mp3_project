# Storage · 介质与原始访问参考

[Storage](storage.md#media) 的硬件参考；模块接口分别读 [SD](../../Components/sd/README.md)、[W25Qxx](../../Components/w25qxx/README.md)、[Platform Flash](../../Platform/flash/README.md)，通用分层/ISR 不在这里重复。

## SD 热插拔与传输

未插卡 Init 成功并进入 NOT_PRESENT，允许整机继续启动；此状态读取块/信息仍失败。稳定插入 Refresh 后进入 READY 或 ERROR，拔出清信息有效性并进入 NOT_PRESENT；传输 BUSY 最终回 READY，错误/超时/中止进入 ERROR，由拔卡或显式重新 Init 恢复，当前无传输中 HAL Abort 与原地重试。

卡检测边沿只是“稳定后复查”：主循环事件槽以 FLAG 合并卡检测和窗口请求，每次检测边沿立即离开就绪、重开消抖静默窗口。任务到期调用 Platform Refresh，只有持续状态变化才发插拔事件；等待时限取消抖/Flash 回收的最近期限，避免只等卡而饿死回收。挂载仍在但消抖中不接受窗口请求，不应误当拔卡清窗。检测、SD DMA、Flash 操作使用各自通知语义，SD/Flash 完成槽在 Init 时由 APP 注入。

SD DiskIO 同步调用经专用 AXI SRAM bounce buffer 分块 DMA：发送前复制并 Clean，接收前 Clean+Invalidate，传输完成后 Invalidate 再复制给 FatFs。缓冲独占 32 B 对齐 Cache line；FatFs 指针不保证对齐或 DMA 可达，不能任意直接传 DMA。IRQ 通知不是卡已可接受下一条命令，写数据完成后还要等卡回 TRANSFER。当前缓冲容量查 Filesystem 配置。

可移除卡使用有返回值的 HAL 初始化，避免 CubeMX `void MX_*` 失败进入 Error_Handler。修改 `.ioc` 的 SDMMC 参数时同步核对 Adapter 初始化参数、NVIC 及 HAL callback 注册开关；按 Handle 注册 Rx/Tx/Error/Abort callback，避免与 BSP 全局回调冲突。Device/Platform 不包含 FreeRTOS，若换裸机，应用承担事件和消抖。错误值/持续状态/底层原因区别沿用 ARC-9。

## NOR 识别与 Quad 协议

启动依次：`0x9F` 读 JEDEC → `0x5A` 验 SFDP → QE → `0xEC` 试读。Platform 注入 Winbond `EF` 与 256 Mbit 容量码 `19`；MemoryType 字节完整缓存但不作为兼容性条件，`EF 40 19` 与 `EF 70 19` 均可通过。SFDP 从 24-bit 地址 0、8 dummy cycle 读四字节签名，仅验证该路径，不据此动态推导所有协议。

QE 已置位不写；未置位且 WIP=0 时 `0x06 → 确认 WEL → 0x31 写原 SR2|QE → 等 WIP=0 → 回读 QE`，初始忙则拒绝。启动失败当前按致命硬件识别错误处理，JEDEC/SR 快照供按需诊断。

| 操作 | 协议与边界 |
| --- | --- |
| 数组读 | `0xEC` 固定 4-byte 地址，1-4-4，四线模式字节 `0xFF` 后独立 4 dummy clocks；同步与 MDMA 共用协议/范围校验 |
| 页编程 | `0x34` 固定 32-bit 单线地址、四线数据；1..256 B 且不跨 256 B 页，确认 WIP=0/QE 后写使能并验证 WEL |
| 擦除 | `0x21` 固定 4-byte 单线地址、无数据，4 KiB 对齐，确认空闲与 WEL |
| 写擦完成 | `0x05` 自动轮询 `(SR1 & 1)==0`，Automatic Stop/Status Match 通知普通上下文收尾 |

模式字节与 dummy 是两个 HAL 字段。原始编程数据发送仍是轮询，不是 MDMA TX；非阻塞体现为后续 WIP 自动轮询。Device BUSY、QSPI BUSY、MDMA 在飞不等于 NOR WIP；普通读不改变 WIP，读完成不查 WIP。Abort 只停控制器，不能撤销 NOR 已开始写擦。

原始数组读取保留 4-byte 首地址对齐语义；上层诊断包装不能放宽 Component 的边界检查。启动 QE 的有限软件轮询与后续写擦的硬件自动状态轮询是两条不同路径，超时现值查各自配置。

MDMA BufferTransferLength 与 QSPI FifoThreshold 必须一致，burst 不超过该值；字节宽度、固定源、递增目标由 CubeMX 管理。更改时同步 `.ioc` 和生成配置；QSPI CS 高电平时间必须满足最严格写事务 50 ns，不能只校验读取吞吐。实测时钟/速度与日志留板级 ticket，不缓存为本轮验收。

## 映射窗口

H7 `0x90000000–0x91FFFFFF` 只读窗口对应物理 32 MiB。开启前检查无在飞操作、WIP=0，并从 Component 获得相同 `0xEC` 描述。映射读不会自动发 `0x05` 等忙，消费者无需每次读前查 WIP，而依赖 Platform 模式所有权。间接访问先 Abort 退出映射，写擦确认 WIP 清零且成功收尾才恢复；独立 MCU 复位也不能假定 NOR 空闲。FTL 保持整笔请求关闭，见 STOR-3。

## 物理分区

地址均含端点；默认 FTL 大小可配置，但首次破坏性操作须核验已有内容和资源/镜像边界。FTL 用分区相对偏移，Bridge 完整检查溢出、范围、对齐后转绝对地址。

| 物理范围 | 用途 |
| --- | --- |
| `0x00000000–0x00000FFF` | 首自检区 |
| `0x00001000–0x00200FFF` | rollback 固件槽，2 MiB |
| `0x00201000–0x00400FFF` | candidate 固件槽，2 MiB |
| `0x00401000–0x007FEFFF` | 默认原始 Resource Pack 保留区 |
| `0x007FF000–0x01FFEFFF` | 默认 24 MiB FTL 物理分区 |
| `0x01FFF000–0x01FFFFFF` | 尾自检区 |

FTL 高端排他边界为 `0x01FFF000`，低端随配置容量变化。固件槽由未来 Bootloader 直接管理，不经 FatFs/FTL；资源是连续原始包，加载已实现、设备侧安装未实现；机内卷用于小文件和安装暂存，SD 承载音乐主库。理由见 [ADR-0009](../adr/0009-w25q256-firmware-slots-and-diagnostic-reservation.md) 与 [ADR-0015](../adr/0015-volume-roles-and-resource-install.md)。

## 原始诊断

破坏性自检仅在明确开关启用、启动早期独占时按首尾区域语义执行：各区擦除 → 地址相关图样逐页编程 → 轮询读回逐字节核对 → MDMA 读回并在 Cache 收尾后逐字节核对。失败不自动重试；共用 Service 唯一订阅，不抢回调槽。它不是普通写入 API，不能借此访问 FTL。

映射基准先以间接读作为独立参照，逐字节比较后才量顺序映射读取。轮询/MDMA/映射测速是端到端吞吐；页编程计时含提交、WIP、通知与任务收尾，不含擦除、图样生成和读回。用 DWT/SystemCoreClock 计时避免 tick 量化，当前尺寸/开关和实现查 [benchmark](../../APP/tasks/storage/benchmark/README.md)。
