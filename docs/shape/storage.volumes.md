# Storage · volumes

可移除状态、卷就绪及文件对象失效。

[返回 Storage](storage.md)

### Rules

- **STOR-8** · provisional · 无卡是可正常启动的持续状态。检测边沿重开任务消抖，卷任务同时关闸：文件和目录调用在 Filesystem 入口等闸，已打开的对象保持有效，已持锁的调用做完；稳定后才改变卷状态，消抖不等于卸载。对账只看卷是否挂着和卡的现状，不靠设备状态变化事件。确认拔出或换卡时先卸载（该卷令牌全部失效、槽释放），再发布卷事件，最后开闸；卷任务不等订阅者。卷事件只有卷就绪和卷消失，形式是叫醒加卷序号，订阅者比对序号判断卷变过没有；调用返回 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 时不当文件错误。_Why:_ 抖动不能伪装传输结果，暂不可读也不等于曲库已消失；别的任务读取失败会先把设备改成无卡，状态变化事件会被吞（#191）；再加应用不改卷任务。_Source:_ [ADR-0024](../adr/0024-file-interface-concurrency.md)、[卷生命周期与卷事件](https://github.com/immorcoding/mp3_project/issues/195)、[SD 生命周期](../../Components/sd/README.md)
- **STOR-9** · provisional · SD 与 Flash 独立初始化、挂载和报告错误，一个卷的启动、等待或故障不占用处理另一个卷的任务：Flash 启动在一次性的 Flash 启动任务中执行，卷任务不等它；每卷各有卷序号。SD 无设备侧格式化，Flash 显式格式化或恢复成功时须已重新挂载、使本卷旧 Token 失效并发布卷事件；APP 自动格式化策略只处理无文件系统结果，损坏、不兼容等其他失败保留可诊断状态；挂载失败不发布卷事件。_Why:_ 一个卷的故障不能污染另一个卷或令旧对象透明继续写；`MountFlash` 实测 3.3–8.3 s，故障时预算 120 s，不能让插拔等它。_Source:_ [Filesystem 接口](../../Service/filesystem/README.md)、[ADR-0024](../adr/0024-file-interface-concurrency.md)、[卷生命周期与卷事件](https://github.com/immorcoding/mp3_project/issues/195)

### Signals

- 2026-11-04 · cite · STOR-8 · 闸和对账都没有上板，这次从 settled 降为 provisional；[0.10.0 spec](https://github.com/immorcoding/mp3_project/issues/202) 板级验收通过后升回 settled，并恢复 _Check:_（存储审阅、卷对账主机测试、拔插项）。
- 2026-11-04 · cite · STOR-9 · 同上，降为 provisional；验收通过后升回 settled，并恢复 _Check:_（存储审阅及独立卷/Token/恢复回归）。
