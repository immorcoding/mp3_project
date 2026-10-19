# Architecture · errors

调用结果、生命周期状态与诊断原因。

[返回 Architecture](architecture.md)

### Rules

- **ARC-9** · settled · 返回值表示本次调用，State 表示持续生命周期，ErrorCode 表示 Component 语义步骤，LastBusStatus/LastPortStatus 表示归一化底层原因；Vendor 原始错误仅用于深入诊断。Adapter 状态转换入口用 `int32_t native_status`，内部再转 SDK 类型，上层不据 HAL 原始码决策。 _Why:_ 一次调用失败、持续状态和底层原因不能相互代替。 _Source:_ [诊断状态对照](sources/diagnostic-observations.md) _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。

### Signals

- 2026-10-19 · cite · new · 播放状态机连跳计数（B5）改在输出 Start 成功（`OutputStarted`）时清零，不再在首帧清零，整库都是不支持采样率时 ALL_BAD 能触发（2244aaf）。
- 2026-10-17 · cite · new · `Service_Playback_BeginStream()` 在模块未 Init 时返回 `SERVICE_NOT_READY`，状态机一律归 STOP_FAILED（AUDIO_FAULT 锁存到重启）；属编程错误、极罕见，未用更精确的状态码区分。
- 2026-10-17 · cite · new · T_out 的“Platform 运行时”由状态机按 `Pump` 回报重建；`ResumeOutput()` 内部 Start 不外传，恢复成功即起算 T_out，半区不满时的等待会算入（至多误报 AUDIO_ERROR 而非 NO_DATA）。外壳无事件时 100 ms 唤醒一次（`PLAYBACK_TASK_WAKE_PERIOD_MS`）作为 T_data/T_out 的到期触发源；空闲开销与板级 T_out 验收待板测。
