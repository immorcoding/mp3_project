# 系统日志与 USB CDC 输出技术文档

> 适用工程：`power`  
> 当前输出后端：USB Full-Speed CDC ACM（虚拟串口）  
> 当前时间源：`HAL_GetTick()`  
> 文档状态：与 2026-07-20 的工程代码同步

## 1. 设计目标

当前日志系统解决两个实际问题：

1. MCU 上电时会立即产生初始化日志，而 Windows 和 VSCode 串口终端通常稍后才打开 COM 口；
2. ST USB CDC 发送是异步过程，应用不能覆盖仍在发送的缓冲区，也不应在主循环中阻塞等待。

因此当前实现采用“生产者入 RAM 队列、主循环非阻塞消费”的方式。启动日志先保存在 MCU RAM 中，等 USB 完成枚举、主机置位 DTR、打开稳定时间结束且 CDC 发送端空闲后，再逐条提交给 USB。

## 2. 模块边界

| 文件 | 职责 | 不负责的内容 |
| --- | --- | --- |
| `System/Log/log.h` | Application 可见的日志公共 API、等级、状态和统计类型。 | USB 类型、Handle 装配、端口缓冲区。 |
| `System/Log/log.c` | 格式化、等级过滤、固定深度环形队列和消费调度。 | 判断 DTR、操作 USB CDC。 |
| `System/Log/log_internal.h` | 内部 Handle、Ops、Context、消息槽位和队列定义。 | Application 公共接口。 |
| `System/Log/log_config.h` | RAM 容量、默认等级、ANSI 颜色等编译期配置。 | 运行期状态。 |
| `System/Log/port/log_port.c/.h` | 绑定 USB 输出和 HAL 时间源，维护 USB 异步发送缓冲区。 | 日志格式和队列溢出策略。 |
| `USB_DEVICE/App/usbd_cdc_if.c/.h` 的 `USER CODE` 区 | 提供 CDC 发送函数、DTR 状态和 `CDC_IsReady_FS()`。 | 日志等级、格式和队列。 |

`USB_DEVICE` 其余部分、`Middlewares/ST/STM32_USB_Device_Library` 和 `Drivers` 为 CubeMX/ST 管理代码。自维护改动必须留在 CubeMX 的 `USER CODE BEGIN/END` 区域。

## 3. 上电调用链

```text
Core/Src/main.c
  -> HAL_Init / SystemClock_Config / MX_GPIO_Init
  -> MX_USB_DEVICE_Init
  -> app_init
       -> LOG_Init
            -> 清零 hlog_default
            -> 装载 LOG_DEFAULT_LEVEL
            -> LOG_Port_Bind
                 -> 清零 log_port_context
                 -> 绑定 log_port_output_ops + log_port_context
                 -> 绑定 LOG_PortGetTimeMs + NULL Context
       -> LOG_Printf("LOG", ...)
            -> 格式化并入队
       -> Board_PMIC_Init
       -> LOG_Printf("PMIC", ...)
            -> 格式化并入队
  -> while (1)
       -> app_run
            -> LOG_Process
                 -> 每次最多尝试提交队首一条
```

`LOG_Printf()` 返回 `LOG_OK` 时，通常只说明消息已进入 RAM 队列。主机实际看到日志要等到后续 `LOG_Process()` 成功提交，并由 USB 完成 IN 传输。

## 4. 公共接口

| 接口 | 能否重复调用 | 作用 |
| --- | --- | --- |
| `LOG_Init()` | 可以，但会清空队列和统计 | 初始化唯一默认实例并绑定端口。 |
| `LOG_SetLevel()` | 可以 | 修改后续新消息的过滤阈值，不清理已入队消息。 |
| `LOG_GetLevel()` | 可以 | 读取当前过滤等级。 |
| `LOG_GetState()` | 可以 | 读取 RESET、READY 或 ERROR。 |
| `LOG_Write()` | 可以 | 复制一段已装配文本到队列。 |
| `LOG_Printf()` | 可以 | 生成前缀、时间戳、Tag、正文和 CRLF，再复制入队。 |
| `LOG_Process()` | 必须高频重复调用 | 非阻塞处理队首一条消息。 |
| `LOG_GetStats()` | 可以 | 取得统计快照，不清零计数器。 |

当前实现不允许应用直接取得 `LOG_HandleTypeDef`、Output Ops、TimeSource 或 Port Context。初始化和装配只通过 `LOG_Init()` 完成，避免外部代码任意替换内部依赖。

## 5. 格式化与等级过滤

标准文本格式：

```text
<等级字符> (<毫秒时间戳>) <Tag>: <正文>\r\n
```

示例：

```text
I (28) PMIC: initialization successful\r\n
```

等级数值由低到高为 `NONE、ERROR、WARN、INFO、DEBUG`。一条消息的等级数值大于当前阈值时会被正常过滤，函数返回 `LOG_OK`，也不会占用队列。

时间戳在 `LOG_Printf()` 格式化时取得，所以表示“消息产生/入队时刻”，不是“USB 发出时刻”。`HAL_GetTick()` 的 32 位自然回绕不会影响日志系统，只是显示值会重新从 0 开始。

`LOG_FORMAT_BUFFER_SIZE` 包含字符串终止符。完整前缀、正文、`\r\n` 和 `\0` 放不下时返回 `LOG_ERROR`，当前策略是不发送截断文本。

## 6. RAM 环形队列

`LOG_QueueTypeDef` 的关键不变量：

- `Messages[LOG_QUEUE_DEPTH]`：固定数量的完整消息槽位；
- `Head`：最旧的待发送消息；
- `Tail`：下一次写入位置；
- `Count`：当前占用槽位数，用于区分空和满；
- Head/Tail 每次移动都对 `LOG_QUEUE_DEPTH` 取模。

每个槽位拥有独立 `Data[]` 副本。因此 `LOG_Printf()` 使用的栈缓冲区在函数返回后可以失效，不会影响稍后的 USB 发送。

队列满时采用“丢最旧、留最新”策略：先移动 Head 并增加 `DroppedCount`，再把新消息写入 Tail。这样更倾向保留最新故障现场，但重要的早期日志仍可能在持续拥塞时被覆盖。

## 7. LOG_Process 的四种端口结果

`LOG_Process()` 一次最多查看并处理队首一条：

| 端口结果 | 队首处理 | 返回值 | 含义 |
| --- | --- | --- | --- |
| `LOG_OUTPUT_OK` | 出队，`SentCount++` | `LOG_OK` | 端口接受了异步发送请求。 |
| `LOG_OUTPUT_BUSY` | 保留 | `LOG_OK` | 发送端暂时忙，下次重试。 |
| `LOG_OUTPUT_NOT_READY` | 保留 | `LOG_OK` | USB/终端尚未就绪，下次重试。 |
| `LOG_OUTPUT_ERROR` | 丢弃，错误计数增加 | `LOG_ERROR` | 不可恢复的本次提交错误；避免坏消息永久堵塞队列。 |

`SentCount` 表示消息已被端口/USB Device 库接受，不是主机应用已经显示该消息的端到端确认。

## 8. Port Context 与异步缓冲区

`LOG_PortContextTypeDef` 包含：

| 字段 | 作用 |
| --- | --- |
| `LastMessage` | 保存最后一条成功提交的无 ANSI 原始日志，便于调试器查看。 |
| `LastMessageLength` | `LastMessage` 的有效长度。 |
| `WriteCount` | USB 接受发送请求的累计次数。 |
| `TxBuffer` | `颜色前缀 + 日志正文 + 颜色复位` 的持久异步发送缓冲区。 |

核心 Queue 和 Port `TxBuffer` 不能简单共用：ST CDC 的 `USBD_CDC_SetTxBuffer()` 只登记指针，不复制数据。直到 IN 端点传输完成，原缓冲区都必须保持有效且不能被覆盖。当前端口只有一个 `TxBuffer`，因此 `CDC_IsReady_FS()` 必须确认上一笔发送已完成，才允许拼接下一条。

ANSI 颜色仅在 Port 层添加：ERROR 红、WARN 黄、INFO 绿、DEBUG 青。队列正文和 `LastMessage` 不包含颜色转义序列。若终端不支持颜色，可将 `LOG_PORT_ANSI_COLOR_ENABLE` 设为 `0U`。

## 9. 为什么只完成 USB 枚举还不够

`CDC_IsReady_FS()` 依次检查：

1. `hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED`：设备完成 USB 配置；
2. `cdc_port_open_fs != 0`：主机通过 CDC `SET_CONTROL_LINE_STATE` 置位 DTR；
3. DTR 上升沿后已超过 `CDC_PORT_OPEN_SETTLE_TIME_MS`；
4. `hUsbDeviceFS.pClassData != NULL`：CDC 类 Handle 已存在；
5. `hcdc->TxState == 0U`：当前没有未完成的 CDC IN 发送。

设备枚举只表示 Windows 已识别 CDC 设备，不表示 VSCode 已经打开 COM6。DTR 更接近终端打开时刻；额外 1000 ms 稳定时间用于规避部分 Windows 串口工具在 DTR 刚置位时还未准备好显示首包的现象。这里没有 `HAL_Delay()`，未满足条件时函数立即返回 0，日志继续留在 RAM 队列。

## 10. DTR 状态如何取得

Cube CDC 接口的 `CDC_Control_FS()` 在收到 `CDC_SET_CONTROL_LINE_STATE` 时，把 `pbuf` 解释为 `USBD_SetupReqTypedef *`，读取 Setup Request 的 `wValue`：

```c
port_open = ((request->wValue & (1U << 0)) != 0U) ? 1U : 0U;
```

bit0 是 DTR。只在状态从 0 变为 1 时记录 `HAL_GetTick()`；DTR 清零代表终端关闭，下次打开会重新开始稳定计时。

这里的 `volatile` 只要求编译器每次实际读取/写入变量，因为写入发生在 USB 控制请求回调路径、读取发生在主循环。它不提供互斥、原子事务或 RTOS 线程安全。

## 11. `TxState` 的来源与精确定义

`__IO uint32_t TxState;` 位于 ST 中间件：

```text
Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc/usbd_cdc.h
USBD_CDC_HandleTypeDef
```

它没有配套的 `enum` 或 `#define` 来声明状态值。当前 ST CDC 源码通过实际赋值定义它的语义：

- `usbd_cdc.c` 的 CDC 类初始化把 `TxState = 0U`；
- `USBD_CDC_TransmitPacket()` 仅在 `TxState == 0U` 时接受请求，并立即置 `TxState = 1U`；
- CDC IN 端点的 `DataIn` 完成路径把 `TxState = 0U`；
- 因此在当前库版本中，`0U` 表示发送空闲，`1U` 表示存在进行中的发送。

`__IO` 来自 CMSIS，对 Cortex-M7 在 `Drivers/CMSIS/Include/core_cm7.h` 中定义为 `volatile`。所以该字段等价于 `volatile uint32_t`：USB 中断/库代码更新后，主循环读取时不会使用被编译器长期缓存的旧值。

这一结论不是来自一个缺失的“状态枚举”，而是直接来自 ST 本地 `usbd_cdc.c` 对字段的初始化、置忙和清忙代码。升级 STM32 USB Device Library 后应重新核对这些位置。

## 12. 统计值如何调试

调用 `LOG_GetStats()` 可取得：

- `PendingCount`：当前队列积压；
- `EnqueuedCount`：累计入队；
- `SentCount`：累计被端口接受；
- `DroppedCount`：队列溢出或端口永久错误造成的丢弃；
- `OutputErrorCount`：端口永久提交错误。

典型判断：

| 现象 | 统计/状态 | 优先检查 |
| --- | --- | --- |
| 启动日志始终不显示 | Pending 增加、Sent 为 0 | 枚举状态、DTR、稳定时间、主循环是否调用 `LOG_Process()`。 |
| 偶发少日志 | Dropped 增加 | 队列深度太小、主循环阻塞、USB 长期未打开。 |
| 一条后全部卡住 | `TxState` 长期非 0 | USB IN 完成中断、USB IRQ、发送缓冲区生命周期。 |
| OutputError 增加 | 永久提交错误 | 参数长度、CDC Handle、USB Device 状态。 |

`log_port_context` 是 `static`，普通 C 代码无法外部引用，但带调试信息的构建通常仍可在调试器按符号/地址查看其 `LastMessage` 和计数。

## 13. 当前限制

1. 只有一个全局默认日志实例；
2. 环形队列和 Port 都没有锁，仅支持单执行上下文；
3. `LOG_Process()` 依赖主循环持续运行，进入关闭中断的 `Error_Handler()` 后队列不会继续排空；
4. 队列满时只记录丢弃数，不为 ERROR 等级预留专用槽位；
5. USB CDC 没有主机端“已显示”确认；
6. 当前只实现输出，没有命令行输入、历史文件日志或掉电保存；
7. `CDC_PORT_OPEN_SETTLE_TIME_MS` 是针对当前 Windows/VSCode 行为的经验值。

## 14. 后续扩展原则

- 增加 UART/SWO 输出：在 Port 层实现新的非阻塞 `TryWrite`，不要修改日志格式和队列；
- 增加 QSPI/SD 历史日志：建议作为独立持久化 Sink，并明确写放大、掉电一致性和刷盘策略；
- 增加 RTOS：为核心队列增加临界区或消息队列，禁止直接从多个任务无锁访问；
- USB 同时需要 CDC 日志和 MSC 文件传输：设备必须改为 CDC+MSC Composite，端点数量、描述符、类路由和缓冲区要统一设计；单独把类切为 MSC 会失去 CDC 日志；
- 增加 DeInit：应先定义“丢弃还是刷出待发送队列”和“正在进行的 USB 传输如何结束”，再实现接口。

## 15. 维护清单

以下变化必须同步更新本文：

- 日志格式、等级、Handle、Ops 或 Context 变化；
- Queue 深度、消息大小、溢出策略或并发模型变化；
- `LOG_Process()` 的消费数量或错误处理策略变化；
- USB DTR、稳定时间或 `CDC_IsReady_FS()` 条件变化；
- ST USB Device Library 升级导致 `TxState` 语义变化；
- 输出后端由 USB CDC 切换或扩展为多 Sink；
- 加入文件系统持久日志、RTOS 或中断日志。

## 16. 代码索引

- 应用入口：[`../APP/app.c`](../APP/app.c)
- 日志公共接口：[`../System/Log/log.h`](../System/Log/log.h)
- 日志核心：[`../System/Log/log.c`](../System/Log/log.c)
- 内部对象模型：[`../System/Log/log_internal.h`](../System/Log/log_internal.h)
- 编译期配置：[`../System/Log/log_config.h`](../System/Log/log_config.h)
- USB 日志 Port：[`../System/Log/port/log_port.c`](../System/Log/port/log_port.c)
- CDC 用户接口：[`../USB_DEVICE/App/usbd_cdc_if.c`](../USB_DEVICE/App/usbd_cdc_if.c)
- ST CDC Handle：[`../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc/usbd_cdc.h`](../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc/usbd_cdc.h)
- ST CDC 状态实现：[`../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c`](../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c)

