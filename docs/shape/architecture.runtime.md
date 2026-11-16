# Architecture · runtime

实例装配与调用期间的对象寿命、中断与任务的交界、调用结果与生命周期状态。

[返回 Architecture](architecture.md)

### Rules

- **ARC-8** · settled · Ops 与 Context 成对绑定且生命周期覆盖调用；Platform 用 static 长期持有 Handle/Context；Bind 只装配且检查 Handle/Ops/Context/资源，Init 再检查必需回调后访问硬件，APP 不调用 Bind。 _Why:_ 调用期间悬空 Context 或装配时访问硬件都会破坏生命周期。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §4 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-17** · provisional · 长期运行的任务及其任务控制块、栈由 APP 装配时静态创建（`xTaskCreateStatic`），其余任务沿用动态创建；新增堆占用须按 HWD-2 审查。 _Why:_ 解码栈约 17.6 KB，超出 32 KiB 堆的余量，扩堆与补丁各有成本。 _Source:_ [ADR-0019](../adr/0019-freertos-static-task-stack.md) _Check:_ code-review 规范轴核对任务创建方式与 FreeRTOS 静态分配开关，依据 [APP/tasks「任务与优先级」](../../APP/tasks/README.md#任务与优先级)与 `APP/tasks/app_tasks.c` 的 `xTaskCreateStatic` 调用。
- **ARC-10** · settled · ISR 只发布轻量事件/FromISR 通知，不格式化日志、发 USB、访问 I2C/SD/文件系统、延时消抖或调用普通 RTOS API；任务执行状态机与业务，通知槽编号只在所属任务内有意义。GPIO EXTI 使用调用者长期持有、普通上下文注册注销的 Callback 链表；其他外设用按 Handle 的强类型回调，不扩成 `IRQ_ID + void *` 通用分发器。 _Why:_ 中断上下文不能承担阻塞业务，通知只唤醒拥有状态的任务。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §8 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-9** · settled · 返回值表示本次调用，State 表示持续生命周期，ErrorCode 表示 Component 语义步骤，LastBusStatus/LastPortStatus 表示归一化底层原因；Vendor 原始错误仅用于深入诊断。Adapter 状态转换入口用 `int32_t native_status`，内部再转 SDK 类型，上层不据 HAL 原始码决策。 _Why:_ 一次调用失败、持续状态和底层原因不能相互代替。 _Source:_ [诊断状态对照](sources/diagnostic-observations.md) _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。

### Proposed

- 2026-11-16 · provisional · ARC-17 补一句：往 Boot 任务里加初始化调用时，先核对“最深调用链（`-fstack-usage` 合并调用图，含静态图里从不执行的分支）加一次任务切换约 204 B”不超过 Boot 栈（192 word，768 B）；newlib-nano 的 `snprintf` 链约 400 B，不在 Boot 里调用，打开 SDRAM 自检要约 852 B，由 `app_tasks.c` 的编译期断言挡住。_Why:_ Boot 自删、Monitor 读不到它的栈，没有运行时读数，只有静态核算；0.10.0 里 Boot 先后被 #211、#215、#218 往里搬初始化，128 word 时会走的最深链余量只有 76 B，才提到 192 word。_Source:_ [卷任务 README](../../APP/tasks/volume/README.md)“默认构建的 Boot 栈核算”；Boot 栈 128 → 192 word 是用户 2026-11-06 的决定。（queued: 无人值守 drain，#211、#215、#218、#220 inbox）
- 2026-11-16 · exploring · 在可被抢占的任务里判超时，先读墙钟、再读完成状态，只有“墙钟已过时限且随后读到的状态仍是忙”才判超时；传给 HAL 内部忙等的软件时限必须远大于调用者所在任务可能被抢占的最长时间。_Why:_ 第 6 次板测抓到两次假超时：`W25Qxx_Process` 先读到忙、被 GUI 抢占几毫秒后读到墙钟已过 5 ms，把已完成的页编程判成超时（已改成先读墙钟）；HAL 的 `QSPI_WaitFlagStateUntilTimeout` 同样先读标志、后读墙钟，后台任务发 256 B 中途被抢占超过 100 ms 就把 `hqspi.State` 置成 ERROR，此后 Flash 到重启都不可用（HAL 不改，只能放大时限：`PLATFORM_FLASH_QSPI_TIMEOUT_MS` 100 → 3000 ms，`FILESYSTEM_FLASH_RECLAIM_TIMEOUT_MS`、`FILESYSTEM_FLASH_RECOVERY_TIMEOUT_MS` 2000 → 5000 ms，服务层单次预算要大于 HAL 单次时限）。同形但未改的位置：SD 适配器的 `sd_stm32_hal_sync` 与三处 `sd_stm32_hal_in_budget`（忙等循环，概率低得多）、`filesystem_flash_transfer_finish` 的服务级时限（比抢占长几个数量级）。_Source:_ [后台任务 README](../../APP/tasks/background/README.md)“Flash 失败诊断”、[Components/w25qxx README](../../Components/w25qxx/README.md)。（queued: 无人值守 drain，#220 inbox 的两条合并；原句建议 writer 评估是否写成 ARC-11 或 code-style 的约定）
- 2026-11-16 · settled · ARC-8 补一句：确需越过 HAL 公开函数时（例如 `sd_stm32_hal_abort_transfer` 为了有上界而绕开 `HAL_SD_Abort`，直接写 SDMMC 寄存器并自己恢复 HAL 句柄的 `State`、`Context`、`ErrorCode`），把所依赖的 HAL 内部事实列在 Adapter README，升级 HAL 时复核。_Why:_ Adapter 不再只是“把请求翻译成 HAL 调用”，依赖清单不写下来，HAL 升级会静默破坏它。_Source:_ [SD Adapter README](../../Adapters/stm32_hal/sd/README.md)。（queued: 无人值守 drain，#209 inbox；ARC-8 是 settled，需用户明确同意才改）

### Signals

- 2026-11-04 · friction · ARC-17 · 0.10.0 合入后，卷、音频读取、前台、后台任务和一次性的 Flash 启动任务都是静态栈（栈在 AXI `.app_axi_ram`），Log、Monitor、GUI 仍是动态栈：长期任务有三个不是静态创建，一次性的 Flash 启动任务不是长期任务却是静态创建，都与规则字面不符。用户决定留给补丁版本一起改：Log、Monitor、GUI 改静态创建，Flash 启动任务改回动态（[地图评论](https://github.com/immorcoding/mp3_project/issues/186#issuecomment-6631678855)）。
- 2026-10-17 · cite · ARC-17 · 播放任务的命令队列与状态信箱也用 `xQueueCreateStatic`，控制块与存储在 DTCM `.bss`（约 360 B），不占内核堆，创建不会失败（#80）。
- 2026-10-17 · cite · new · `playback_task()` 在队列创建、播放模块初始化或音频回调注册失败时记日志并 `vTaskDelete(NULL)`（`FreeRTOSConfig.h` 未开 `INCLUDE_vTaskSuspend`），快照保持全零；用户可能日后改为 `Error_Handler()`（同 `app_tasks.c` 创建失败）。该路径主机测试无法覆盖。
- 2026-10-09 · cite · ARC-10 · 音频路径的欠载与输出看门狗 T_out 都由播放任务判定；ISR 只计数并唤醒，不写 XSMT、不调 Stop；I2S2 的 DMA 流与 NVIC 须从源工程导出，优先级在 5–15。
- 2026-10-17 · cite · new · `Service_Playback_BeginStream()` 在模块未 Init 时返回 `SERVICE_NOT_READY`，状态机一律归 STOP_FAILED（AUDIO_FAULT 锁存到重启）；属编程错误、极罕见，未用更精确的状态码区分。
- 2026-10-17 · cite · new · T_out 的“Platform 运行时”由状态机按 `Pump` 回报重建；`ResumeOutput()` 内部 Start 不外传，恢复成功即起算 T_out，半区不满时的等待会算入（至多误报 AUDIO_ERROR 而非 NO_DATA）。外壳无事件时 100 ms 唤醒一次（`PLAYBACK_TASK_WAKE_PERIOD_MS`）作为 T_data/T_out 的到期触发源；空闲开销与板级 T_out 验收待板测。
