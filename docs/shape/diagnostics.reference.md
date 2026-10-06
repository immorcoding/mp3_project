# Diagnostics reference

[Diagnostics](diagnostics.md) 的调试解释；公开枚举和值以各模块头文件为准。

## errors

按 [Architecture](architecture.md) ARC-9 区分四类观察：调用返回值说明本次结果；State 是持续生命周期；ErrorCode 指失败的语义步骤；LastBusStatus/LastPortStatus 指归一化底层原因。Device 内部失败时底层状态可仍为 OK，不能把它解释成没有错误。

调试时依次看 State → ErrorCode → LastBusStatus/LastPortStatus；AXP2101 另看 LastFailedRegister；仍不足时才看具体后端的原始错误，例如 I2S/SD HAL Handle 或 Platform Power 私有 SoftI2C Handle。原始位图用于深入定位，不成为上层控制输入。

设备特有枚举与失败语义分别见 [Audio](../../Components/audio/README.md)、[AXP2101](../../Components/axp2101/README.md)、[SD](../../Components/sd/README.md)。Audio 没有 I2C NACK；AXP2101 保留失败寄存器；SD Init 在无卡时可返回成功且 State 为 NOT_PRESENT，之后无卡读块仍失败。成功返回和可立即执行其他操作并非同义。

[FTL](../../Components/flash_ftl/README.md) 区分请求受理、仍在飞和最终结束；非法参数/忙拒绝不允许另一请求覆盖现有操作。失败返回不保证介质未改变，提交成功但通知丢失仍可报告失败。最新已提交记录损坏不静默回退，格式不兼容/格式化未完不自动重格式化；格式与恢复约束由 FTL 正文维护。

软件超时、控制器/DMA 停止与 NOR 内部操作结束是三个条件。安全收尾未确认前不复用缓冲，不在 NOR 状态未知时恢复映射，无法确认就保持隔离；关闭可选日志不会屏蔽这些错误。

## logging

局部接口分别在 [Components Log](../../Components/log/README.md)、[Service Log](../../Service/log/README.md)、[Platform Log](../../Platform/log/README.md) 和 [Log Task](../../APP/tasks/log/README.md)。核心只处理格式化、过滤和 RAM 队列，具体输出由 Platform 绑定 Adapter（[ADR-0004](../adr/0004-log-single-consumer-and-output-adapter.md)）。

调度器启动前，Platform 初始化输出并清空核心队列/统计，启动代码可串行直接入核心。创建任何日志生产任务之前先初始化 Service 的静态消息池和 free/ready 队列；调度器启动后走普通任务 → Service_Log_Post → Log Task → 核心 → 输出 Adapter。

消息所有权为 `free → producer → ready → Log task → free`。队列传消息块指针，text 已复制，tag 仍是原指针。Log Task 推进输出，核心有空间才取 ready 块，转交后归还；Service 池耗尽立即丢弃当前消息，业务任务不等待输出。核心队列满则丢最旧消息保留最新故障，这与 Service 池满策略不同。

输出返回 OK 时核心出队；BUSY/NOT_READY 保留队首稍后重试；ERROR 记录后丢弃，避免永久堵塞。LOG_OK 和 SentCount 表示入队/输出接受，不证明 PC 已显示。

USB CDC 提交会继续读取指针，因此 Adapter 的 TxBuffer 与核心槽位分离且 Context 长期存活。就绪同时取决于枚举、主机 DTR、打开后的稳定时间与发送空闲；未就绪是可重试暂态，使延后打开串口仍可见排队的启动日志。

ANSI 装饰只写 Adapter TxBuffer，不改变核心原消息或 LastMessage；等级颜色、消息长度、深度和统计字段由 [核心配置](../../Components/log/log_config.h) 与 [USB Adapter](../../Adapters/stm32_hal/log_usb_cdc/README.md) 维护。替换输出实现现有 Ops 并由 Platform 绑定，核心不硬编码 USB/UART 组合。
