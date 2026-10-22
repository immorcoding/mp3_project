# Architecture · runtime

实例装配与调用期间的对象寿命、中断与任务的交界、调用结果与生命周期状态。

[返回 Architecture](architecture.md)

### Rules

- **ARC-8** · settled · Ops 与 Context 成对绑定且生命周期覆盖调用；Platform 用 static 长期持有 Handle/Context；Bind 只装配且检查 Handle/Ops/Context/资源，Init 再检查必需回调后访问硬件，APP 不调用 Bind。 _Why:_ 调用期间悬空 Context 或装配时访问硬件都会破坏生命周期。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §4 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-17** · provisional · 长期运行的任务及其任务控制块、栈由 APP 装配时静态创建（`xTaskCreateStatic`），其余任务沿用动态创建；新增堆占用须按 HWD-2 审查。 _Why:_ 解码栈约 17.6 KB，超出 32 KiB 堆的余量，扩堆与补丁各有成本。 _Source:_ [ADR-0019](../adr/0019-freertos-static-task-stack.md) _Check:_ code-review 规范轴核对任务创建方式与 FreeRTOS 静态分配开关，依据 [APP/tasks「任务与优先级」](../../APP/tasks/README.md#任务与优先级)与 `APP/tasks/app_tasks.c` 的 `xTaskCreateStatic` 调用。
- **ARC-10** · settled · ISR 只发布轻量事件/FromISR 通知，不格式化日志、发 USB、访问 I2C/SD/文件系统、延时消抖或调用普通 RTOS API；任务执行状态机与业务，通知槽编号只在所属任务内有意义。GPIO EXTI 使用调用者长期持有、普通上下文注册注销的 Callback 链表；其他外设用按 Handle 的强类型回调，不扩成 `IRQ_ID + void *` 通用分发器。 _Why:_ 中断上下文不能承担阻塞业务，通知只唤醒拥有状态的任务。 _Source:_ [architecture_standard（迁移基线）](https://github.com/immorcoding/mp3_project/blob/232502bd4b66a728d26d58d39555f34bb3faedf2/docs/architecture_standard.md) §8 _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
- **ARC-9** · settled · 返回值表示本次调用，State 表示持续生命周期，ErrorCode 表示 Component 语义步骤，LastBusStatus/LastPortStatus 表示归一化底层原因；Vendor 原始错误仅用于深入诊断。Adapter 状态转换入口用 `int32_t native_status`，内部再转 SDK 类型，上层不据 HAL 原始码决策。 _Why:_ 一次调用失败、持续状态和底层原因不能相互代替。 _Source:_ [诊断状态对照](sources/diagnostic-observations.md) _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。

### Signals

- 2026-10-17 · cite · ARC-17 · 播放任务的命令队列与状态信箱也用 `xQueueCreateStatic`，控制块与存储在 DTCM `.bss`（约 360 B），不占内核堆，创建不会失败（#80）。
- 2026-10-17 · cite · new · `playback_task()` 在队列创建、播放模块初始化或音频回调注册失败时记日志并 `vTaskDelete(NULL)`（`FreeRTOSConfig.h` 未开 `INCLUDE_vTaskSuspend`），快照保持全零；用户可能日后改为 `Error_Handler()`（同 `app_tasks.c` 创建失败）。该路径主机测试无法覆盖。
- 2026-10-09 · cite · ARC-10 · 音频路径的欠载与输出看门狗 T_out 都由播放任务判定；ISR 只计数并唤醒，不写 XSMT、不调 Stop；I2S2 的 DMA 流与 NVIC 须从源工程导出，优先级在 5–15。
- 2026-10-17 · cite · new · `Service_Playback_BeginStream()` 在模块未 Init 时返回 `SERVICE_NOT_READY`，状态机一律归 STOP_FAILED（AUDIO_FAULT 锁存到重启）；属编程错误、极罕见，未用更精确的状态码区分。
- 2026-10-17 · cite · new · T_out 的“Platform 运行时”由状态机按 `Pump` 回报重建；`ResumeOutput()` 内部 Start 不外传，恢复成功即起算 T_out，半区不满时的等待会算入（至多误报 AUDIO_ERROR 而非 NO_DATA）。外壳无事件时 100 ms 唤醒一次（`PLAYBACK_TASK_WAKE_PERIOD_MS`）作为 T_data/T_out 的到期触发源；空闲开销与板级 T_out 验收待板测。
