# Architecture · errors

调用结果、生命周期状态与诊断原因。

[返回 Architecture](architecture.md)

### Rules

- **ARC-9** · settled · 返回值表示本次调用，State 表示持续生命周期，ErrorCode 表示 Component 语义步骤，LastBusStatus/LastPortStatus 表示归一化底层原因；Vendor 原始错误仅用于深入诊断。Adapter 状态转换入口用 `int32_t native_status`，内部再转 SDK 类型，上层不据 HAL 原始码决策。 _Why:_ 一次调用失败、持续状态和底层原因不能相互代替。 _Source:_ [诊断状态对照](sources/diagnostic-observations.md) _Check:_ `CODING_STANDARDS.md` 的架构审阅入口。
