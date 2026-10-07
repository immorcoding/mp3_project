# 错误观察对照

[Architecture](../architecture.md) ARC-9 是错误模型的唯一规则；本表只补充调试时不能从单个返回值推断的情况。

| 观察 | 能得出的结论 | 不能得出的结论 | 定义源 |
| --- | --- | --- | --- |
| ErrorCode非空，LastBusStatus/LastPortStatus仍OK | 失败可能发生在Device内部语义步骤 | 设备整体没有错误 | [Audio实现](../../../Components/audio/audio.c)、[AXP2101实现](../../../Components/axp2101/axp2101.c) |
| SD初始化成功，State为NOT_PRESENT | 无卡是被识别出的持续状态 | 之后可以读写块 | [SD](../../../Components/sd/README.md) |
| AXP2101的LastFailedRegister有记录 | 可定位最近总线失败的寄存器 | 上层应按原始后端位图控制业务 | [AXP2101](../../../Components/axp2101/README.md) |
| FTL返回失败 | 请求未向调用者确认成功 | 介质从未改变或可以立刻复用缓冲 | [FTL](../../../Components/flash_ftl/README.md)、[Storage](../storage.md) |
| 软件超时 | 等待预算耗尽 | 控制器/DMA停止或NOR内部操作结束 | [Storage](../storage.md) STOR-3 |
| LOG_OK或SentCount增加 | 消息入队或Adapter接收 | 主机已收到/显示 | [Log](../../../Components/log/README.md)、[Diagnostics](../diagnostics.md) DIAG-2 |

排查按 State → ErrorCode → 归一化后端结果定位；AXP2101再看失败寄存器，仍不足才看I2S/SD HAL或SoftI2C私有原始错误。完整枚举和位图由头文件维护，不在此复制。
