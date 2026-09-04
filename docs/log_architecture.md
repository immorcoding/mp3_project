# 日志组件架构

> 适用工程：`version0.3.1` 及后续版本
>
> 当前输出：USB CDC
>
> 当前模型：日志核心固定深度 RAM 队列、Service_Log 静态消息块池、普通任务非阻塞投递、Log Task 单消费者、Platform 装配输出 Adapter
>
> 相关 ADR：[ADR-0004：日志核心单消费者与可替换输出 Adapter](adr/0004-log-single-consumer-and-output-adapter.md)

## 1. Module 所有权与装配

```text
APP 启动阶段 / Platform 启动路径
       -> Platform_Log_Init / LOG_Printf（调度器启动前）
普通 FreeRTOS Task
       -> Service_Log_Post
Service/log + Log Task
       -> Service_Log_Consume -> LOG_Printf / LOG_Process
Components/log
       -> LOG_OutputOpsTypeDef
Adapters/stm32_hal/log_usb_cdc
       -> CDC_Transmit_FS / CDC_IsReady_FS / HAL_GetTick
Platform/log
       -> 持有 Adapter 实例并完成 Bind + LOG_Init
```

| 文件 | 职责 |
| --- | --- |
| `Components/log/log.h` | 普通调用者使用的等级、状态、统计和生产/消费 API。 |
| `Components/log/log_adapter.h` | Component 拥有的输出 Ops、输出绑定、时间源绑定和装配初始化接口。 |
| `Components/log/log_internal.h` | 私有 Handle、消息槽位和队列结构。 |
| `Components/log/log_config.h` | 日志等级、消息长度和队列深度。 |
| `Components/log/log.c` | 格式化、过滤、RAM 队列和消费调度。 |
| `Adapters/stm32_hal/log_usb_cdc/log_usb_cdc_stm32_hal_adapter.*` | USB 就绪判断、异步缓冲、ANSI 颜色和 HAL 时间源。 |
| `Platform/log/platform_log.*` | 持有具体 Adapter，并调用 Bind 和 `LOG_Init()`。 |
| `Service/log/log_service.*` | FreeRTOS 静态消息块池、free/ready queue，以及任务间日志投递 Interface。 |
| `APP/tasks/log/log_task.*` | 唯一调用 `Service_Log_Consume()` 的 Log Task。 |

日志核心不包含 USB、HAL 或 CubeMX 头文件。USB Adapter 不访问日志内部 Handle。

上图列出 Module 角色与装配关系，不表示统一的 `#include` 或单一路径。日志的编译期依赖遵循 [architecture_standard.md](architecture_standard.md)：Log Component 拥有输出 Ops，USB Adapter 实现 Ops，Platform 持有并绑定 Adapter Context，Service/APP 只使用各自公开 Interface。

## 2. 初始化运行时路径

```text
app_init
  -> Platform_Log_Init
       -> LOG_UsbCDC_STM32HALAdapter_Bind
            -> 清空 Platform 持有的 USB Adapter Context
            -> 生成 Output = Ops + Context
            -> 生成 TimeSource = GetTimeMs + Context
       -> LOG_Init(&Output, &TimeSource)
            -> 清空日志队列和统计
            -> 校验 Ops、Context 和时间源
            -> 复制绑定
            -> State = READY
  -> LOG_Printf("initialization successful")
       -> 日志先进入 RAM 队列
  -> Platform_Init() 期间的启动日志继续直接进入日志核心
  -> app_task_start()
       -> Create Task
            -> Service_Log_Init()
                 -> 创建 free queue 与 ready queue
                 -> 将全部静态消息块放入 free queue
            -> 创建 Log Task、Storage Task 与 Monitor Task
```

`LOG_Init()` 不再选择所谓“默认 Backend”。选择 USB、UART、RTT 或文件输出属于
Platform 装配策略。

## 3. 生产与消费

日志核心的生产路径：

```text
LOG_Printf
  -> 获取消息产生时刻的时间戳
  -> 拼接 Level、timestamp、Tag、正文和 CRLF
  -> LOG_Write
  -> 复制进固定槽位 RAM 队列
```

日志核心的消费路径：

```text
LOG_Process
  -> 查看队首
  -> Output.Ops->TryWrite
       -> LOG_OUTPUT_OK        出队
       -> LOG_OUTPUT_BUSY      保留，稍后重试
       -> LOG_OUTPUT_NOT_READY 保留，稍后重试
       -> LOG_OUTPUT_ERROR     记录并丢弃，避免永久堵塞后续消息
```

`LOG_OK` 表示消息成功入队或被输出 Adapter 接受，不表示 PC 终端已经显示。

调度器启动后，普通任务不直接并发调用日志核心，而通过 Service_Log 投递：

```text
Service_Log_Post(level, tag, text)
  -> 从 free queue 取得一个静态消息块指针
  -> 复制 level、tag 指针和以 '\0' 结尾的 text
  -> 将同一消息块指针送入 ready queue

Log Task
  -> Service_Log_Consume()
       -> LOG_Process() 推进 USB 输出
       -> 若 Components/log 的 RAM 队列未满，则从 ready queue 取得一个消息块
       -> LOG_Printf() 将该消息转交给日志核心
       -> 将同一消息块指针归还 free queue
```

两个 FreeRTOS 队列存放的是消息块**指针值**，不复制完整正文。一个消息块在任意时刻只属于
`free queue`、一个生产者局部变量或 `ready queue` 三者之一；这使任务间投递具备明确的
所有权流转，同时避免每次队列操作复制 128 B 文本。

## 4. 为什么需要两个缓冲区

日志核心的队列槽位保存“等待提交”的完整消息；USB Adapter 的 `TxBuffer` 保存
“已经提交给 USB，但异步传输尚未完成”的数据。

两者不能共用：

1. `CDC_Transmit_FS()` 返回后 USB 仍可能继续读取指针；
2. 日志核心在消息出队后可以复用该队列槽位；
3. 若直接把队列槽位交给 USB，复用时会修改正在发送的数据。

因此 Platform 持有的 `LOG_UsbCDC_STM32HALAdapterTypeDef` 必须具有静态生命周期。

## 5. USB 就绪和 DTR

USB Adapter 调用 `CDC_IsReady_FS()`。该函数综合判断：

- USB Device 已枚举；
- 主机打开了虚拟串口并设置 DTR；
- 打开后的稳定时间已满足；
- CDC 发送状态当前空闲。

未就绪属于可重试暂态，返回 `LOG_OUTPUT_NOT_READY`，日志核心保留队首。因此即使
MCU 先启动、VS Code 稍后手工打开 COM 口，启动日志仍可以在端口就绪后出现。

## 6. ANSI 颜色

颜色只属于 USB Adapter：

| 等级 | ANSI 颜色 |
| --- | --- |
| ERROR | 红色 |
| WARN | 黄色 |
| INFO | 绿色 |
| DEBUG | 青色 |

Adapter 在 `TxBuffer` 中拼接颜色前缀和复位序列，不修改日志核心中的原始消息，
也不污染 `LastMessage` 调试快照。

`LOG_USB_CDC_STM32_HAL_ADAPTER_ANSI_COLOR_ENABLE` 可以在编译期关闭。

## 7. RAM 与统计

核心配置：

| 宏 | 当前值 | 作用 |
| --- | ---: | --- |
| `LOG_FORMAT_BUFFER_SIZE` | 256 | 单条完整消息缓冲区，包含 `'\0'`。 |
| `LOG_QUEUE_DEPTH` | 5 | 固定日志槽位数量。 |
| `LOG_DEFAULT_LEVEL` | INFO | 上电默认过滤等级。 |

统计：

| 字段 | 含义 |
| --- | --- |
| `PendingCount` | 当前待提交消息数。 |
| `EnqueuedCount` | 累计入队数量。 |
| `SentCount` | 累计被 Adapter 接受的数量。 |
| `DroppedCount` | 队列溢出或永久输出错误造成的丢弃数量。 |
| `OutputErrorCount` | Adapter 永久提交错误数量。 |

队列满时当前策略是丢弃最旧消息，优先保留最新故障现场。

## 8. RTOS 约束

当前 Log Component 本身没有锁，因此其调用权由 Service_Log 和启动阶段约束，而不是假设
`LOG_Printf()` 可以从多个任务并发调用：

1. `Service_Log_Init()` 必须在 Create Task 中先完成，再创建任何可能调用 `Service_Log_Post()` 的任务；
2. 调度器启动后，普通任务使用 `Service_Log_Post()`，不得直接调用 `LOG_Printf()`、`LOG_Write()` 或 `LOG_Process()`；
3. 只有 Log Task 调用 `Service_Log_Consume()`，并因此成为调度器启动后的日志核心消费者；
4. ISR 不调用 `Service_Log_Post()`、`snprintf()`、USB CDC 或普通日志 API；ISR 只能发布极短通知，由普通任务随后记录日志；
5. `tag` 是指针，不会复制到消息块；调用者必须传入静态存储期字符串，例如字符串字面量；
6. `Service_Log_Post()` 从不等待空闲块。消息池耗尽时当前消息被丢弃，业务任务不会因日志输出而阻塞。

启动阶段的 `app_init()` 和 Platform 初始化仍可直接使用 `LOG_Printf()`，因为此时调度器尚未
启动，不存在多个任务并发访问日志核心的情况。

## 9. 新增输出 Adapter

以 UART 为例：

1. 新建 `Adapters/stm32_hal/log_uart/`；
2. 实现 `LOG_OutputOpsTypeDef::TryWrite`；
3. 定义 UART Context 和持久发送缓冲；
4. 在 Adapter Bind 中生成 `LOG_OutputTypeDef`；
5. 在 `Platform_Log_Init()` 中替换或组合输出；
6. 不修改 `Components/log/log.c`。

若未来需要多个输出同时接收同一条日志，应新增 Fan-out Component 或在 Platform
绑定组合 Adapter，不要让日志核心硬编码 USB + UART。

## 10. 代码索引

- [`../Components/log/log.h`](../Components/log/log.h)
- [`../Components/log/log_adapter.h`](../Components/log/log_adapter.h)
- [`../Components/log/log.c`](../Components/log/log.c)
- [`../Adapters/stm32_hal/log_usb_cdc/log_usb_cdc_stm32_hal_adapter.c`](../Adapters/stm32_hal/log_usb_cdc/log_usb_cdc_stm32_hal_adapter.c)
- [`../Platform/log/platform_log.c`](../Platform/log/platform_log.c)
- [`../Service/log/log_service.h`](../Service/log/log_service.h)
- [`../Service/log/log_service.c`](../Service/log/log_service.c)
- [`../APP/tasks/log/log_task.c`](../APP/tasks/log/log_task.c)
