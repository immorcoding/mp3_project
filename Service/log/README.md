# Service_Log

Service_Log 将多个普通任务的短日志文本汇集到静态消息块池，并由 Log task 顺序转交给 `Components/log`。它解决多任务生产与单一输出消费者之间的同步，不取代日志核心的格式化和 USB 输出能力。

## 公开 Interface

- `Service_Log_Init()`：创建 free queue、ready queue 并归入全部静态消息块；
- `Service_Log_Post()`：非阻塞投递一条已格式化的文本；
- `Service_Log_Consume()`：由 Log task 推进输出，并至多转交一个 ready 消息；
- `Service_Log_GetDropStats()`：读日志丢失累计（DIAG-6、#204），任意普通任务可调；供 Monitor 的 `Health` 行。`ServiceDropped` 是 `Service_Log_Post()` 没能交给 Log task 的条数（消息池满返回 `SERVICE_BUSY`、队列转移失败、文本过长），多个生产者任务在临界区内自增；`CoreDropped` 是 `LOG_StatsTypeDef.DroppedCount`（核心队列溢出或发送错误）的副本，由 Log task 在每次 `Service_Log_Consume()` 里刷新，所以调用者不碰只许 Log task 访问的日志核心（DIAG-1）。参数无效（空指针、非法等级）与尚未初始化不计入丢失。两项各自单调不降，32 位回绕时用无符号差值。

所有公开函数返回 `Service_StatusTypeDef`；`Service_Log_Post()` 的日志等级使用
`Service_Log_LevelTypeDef`，不向调用者泄漏 `Components/log` 的类型。

## 编译期依赖

- `LOG_Process()`、`LOG_Printf()`、`LOG_GetStats()`；
- FreeRTOS queue Interface。

## 运行时请求与事件路径

普通 Task 经 `Service_Log_Post()` 投递，Log task 经 `Service_Log_Consume()` 向下调用日志核心。当前没有硬件 ISR 直接进入本 Module；ISR 如需日志只能先通知普通任务。

## 资源与约束

- 队列深度宏在 `log_service_config.h`，由 `log_service.h` 再导出；其他 Module 不得直接包含 config。
- 所有权路径固定为 `free → producer → ready → Log task → free`；
- `tag` 使用静态存储期字符串（通常是字符串字面量），避免异步投递后调用者栈或临时对象失效；
- 仅普通任务可调用，不支持 ISR；队列满时不等待并返回错误。
- `Service_Log_Init()` 必须早于所有可能投递日志的任务。
- **消息池深度与节拍（DIAG-3、#204）**：池深 `LOG_SERVICE_QUEUE_LENGTH` 为 45 块，每块正文 128 B（#218 从 30 块加大，用户 2026-11-06 的决定；多 15 块约 2 KiB DTCM）。Log task 每 5 ms 一轮，每轮至多把一个 ready 消息转交日志核心（核心队列深度 5，满时暂不取），所以稳态排空速度至多约 200 行/s（USB 主机在读时）。一次突发的行数 B 约在 B × 5 ms 内排空；突发加上同一时刻其他任务的投递要少于 45 才不丢。当前最大的周期突发是 Monitor 每 5 s 的 16 行（开机后 17 行，约 85 ms 排空），核算见 [Monitor](../../APP/tasks/monitor/README.md) 与 [前台任务](../../APP/tasks/foreground/README.md)“日志与消息池核算”：保守口径下最坏同时到达 34 行（Monitor 17 + Filesystem 持锁统计 3 + 曲库短锁 1 + `ui_cell` 持锁统计 1 + 显示延迟汇总 1 + 前台 step 耗时 1 + 一次拔卡 8 + `SERVICE_BUSY` 2，音频读取任务与前台任务各 1 行），留 11 块（约 170 ms 排空）。Debug 构建另有 Filesystem 的持锁统计（标签 `FSLOCK`，见 [Filesystem](../filesystem/README.md)“持锁统计”）：每 10 s 在某次文件接口调用放锁后投一批 3 行，每行不超过约 80 个字符。Release 不投 `FSLOCK`、`CATLOCK`、`UICELL`、`DISPLAY`。USB 主机不读时，核心队列 5 条加池内 45 块之后继续投递就会被丢弃并计入 `ServiceDropped`。新增汇总类日志须注明节拍并核算，`monitor_task.c` 的 `_Static_assert` 核对 `MONITOR_TASK_CONCURRENT_MAX + 5 + MONITOR_POOL_OTHER_WORST_LINES + MONITOR_POOL_SPARE_MIN ≤ LOG_SERVICE_QUEUE_LENGTH`。

## 开机早期的丢行（每次开机 `Health` 的 `log_core=5(+5)`）

调度器启动前，`main()` 里的 `LOG_Printf` 直接写日志核心队列（Log Task 还不存在，没人取），队列深度 `LOG_QUEUE_DEPTH` 是 5，满了丢最旧的并计入 `DroppedCount`。这一段启动序列共写 10 条 INFO：`LOG: Initialization successful.`、`PLATFORM: Initialization successful.`、`RESOURCE: RPKC1 …`、`RESOURCE: Loaded id=1` 至 `id=5`、`RESOURCE: Initialization time=…`、`RESOURCE: Initialization successful.`。留下最新 5 条，丢掉前 5 条：LOG、PLATFORM 两行，RPKC1 一行，`Loaded id=1`、`id=2`。所以每次开机的串口日志都从 `RESOURCE: Loaded id=3` 开始，Monitor 的第一批 `Health` 是 `log_core=5(+5)`，之后是 `(+0)`。

- **`BOOT: FW` 没丢**：它是调度器启动之后由 Create Task 经 `Service_Log_Post()` 投的，走 45 块的 Service 消息池，不经过核心队列在 Log Task 起来之前的堆积。`HOOK:` 行同理（在后台任务、前台任务或音频读取任务里投）。
- **不是 #207–#209 引入的**：LOG/PLATFORM 两行自 2026-07-29、`RPKC1` 与 `Loaded` 行自 2026-09-01、队列深度 5 自 2026-08-30 就是这样；`APP/app.c`、`Components/log`、`Service/resource`、`Platform/log` 自 0.10.0 的 spec 提交起未改。
- **判读 R1.3**：开机批次的 `log_core=5(+5)` 是基线，不算日志丢失；之后任何 `(+n)` 才算。
- 依据是读代码加日志首行（保留的 5 条正好从 `Loaded id=3` 起），没有加打点实测。要让这 5 行也出得来，要么调大 `LOG_QUEUE_DEPTH`（占 RAM），要么把启动序列的汇总行压少，另开票。

## 命名

公开 Interface 使用 `Service_Log_*`；私有类型、静态对象和局部变量使用 `snake_case`。
