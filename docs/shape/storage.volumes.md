# Storage · volumes

可移除状态、卷就绪及文件对象失效。

[返回 Storage](storage.md)

### Rules

- **STOR-8** · provisional · 无卡是可正常启动的持续状态。检测边沿重开任务消抖，卷任务同时关闸：文件和目录调用在 Filesystem 入口等闸，已打开的对象保持有效，已持锁的调用做完；对账期间又来新边沿，闸保持关闭并重新消抖；闸没有超时放行也没有看门狗，每次暂停在所有路径上都配一次恢复；稳定后才改变卷状态，消抖不等于卸载。对账只看卷是否挂着和卡的现状，不靠设备状态变化事件；刷新失败时卡状态不可信：卷挂着一律卸载，卷没挂不动作、不挂载。确认拔出或换卡时先卸载（该卷令牌全部失效、槽释放），再发布卷事件，最后开闸；卷任务不等订阅者。卷事件只有卷就绪和卷消失，形式是叫醒加卷序号，订阅者比对序号判断卷变过没有；卷没挂时按路径或卷操作的入口返回 `NOT_READY`，按句柄操作的入口返回 `INVALID_HANDLE`；调用返回 `NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE` 且卷变了（序号不同或卷已不挂，已等过本轮对账）时不当文件错误，卷没变（卡还在、设备出错）时按文件错误处理，调用者先对同一句柄 `CloseFile`（在入口等闸，借此等过本轮对账）再比对卷序号，读取任务另有 100 ms 的卷宽限。_Why:_ 抖动不能伪装传输结果，暂不可读也不等于曲库已消失；别的任务读取失败会先把设备改成无卡，状态变化事件会被吞（#191）；再加应用不改卷任务；`NO_MEDIA` 有两种来源（锁内看脚发现卡不在；`FR_NOT_READY` 映射，卡在设备出错），一律等卷事件会把“静音到 T_data（5 s）才报 NO_DATA”带回来；读取任务与卷任务同级（3），卡检测中断不会立即切到同级的卷任务，读取任务收尾时边沿到了而闸还没关，所以“卷没变”不等于“卡还在”，要宽限一小段。 _Source:_ [ADR-0024](../adr/0024-file-interface-concurrency.md)、[卷生命周期与卷事件](https://github.com/immorcoding/mp3_project/issues/195)、[SD 生命周期](../../Components/sd/README.md)、[卷任务](../../APP/tasks/volume/README.md)
- **STOR-9** · settled · SD 与 Flash 独立初始化、挂载和报告错误，一个卷的启动、等待或故障不占用处理另一个卷的任务：Flash 启动在一次性的 Flash 启动任务中执行，卷任务不等它；每卷各有卷序号。SD 无设备侧格式化，Flash 显式格式化或恢复成功时须已重新挂载、使本卷旧 Token 失效并发布卷事件；APP 自动格式化策略只处理无文件系统结果，损坏、不兼容等其他失败保留可诊断状态；挂载失败不发布卷事件。_Why:_ 一个卷的故障不能污染另一个卷或令旧对象透明继续写；`MountFlash` 实测 3.3–8.3 s，故障时预算 120 s，不能让插拔等它。_Source:_ [Filesystem 接口](../../Service/filesystem/README.md)、[卷任务](../../APP/tasks/volume/README.md)、[ADR-0024](../adr/0024-file-interface-concurrency.md)、[卷生命周期与卷事件](https://github.com/immorcoding/mp3_project/issues/195) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及独立卷/Token/恢复回归。

### Proposed

- 2026-11-16 · provisional · STOR-8 的 _Why:_ 补一句：卷任务从刷新到卸载是两个独立的持锁入口（`RefreshSD` 与 `UnmountSD`），放锁到再拿锁之间已通过闸、正在等锁的调用理论上可能抢到 SD 卷锁（要在两条语句之间被挂起超过整个静默期，实际不成立）；后果是读取任务读 1 块新卡上按旧簇号算出的垃圾字节，随旧流序号作废，只读不写，不会写卡；新卡比旧卡小时越界读可能使设备进 ERROR，要重新插拔才恢复。彻底关掉需要 Service 提供“刷新加卸载”的合并入口（公开接口变化），是否做由用户决定。_Source:_ [音频读取任务 README](../../APP/tasks/audio_reader/README.md)“`RefreshSD` 与 `UnmountSD` 之间的空隙”。（queued: 无人值守 drain，#211、#215 inbox）
- 2026-11-16 · provisional · STOR-9 补一句：Flash 卷挂载位为真之前，不调用 Flash 生命周期入口（回收、诊断）。_Why:_ `MountFlash` 持 Flash 卷锁 3.3 至 8.3 s，生命周期入口不走“拿锁前先看卷没挂”的进门序列，会陪它等完整段，拖住别的 Flash 调用者；后台任务的回收作业已按卷序号为 0 或卷没挂载直接等卷事件。（queued: 无人值守 drain，#211 inbox）

### Signals

- 2026-11-16 · cite · STOR-8 · 闸和对账是新设计，用户 2026-11-04 决定从 settled 降为 provisional；第 7 次板测（#221）R3.1、R3.3 ②、R3.4、R3.5、R4.5 至 R4.9 通过（52 次拔插收尾 30–33 ms，30 次假抖动只走 `gate_only`），但 R3.2、R3.3 ①③（30 ms 内抽插，带锁卡槽机械上做不到）未测、0.8.0 I11 判“部分”，所以暂不升回（失败约定已按实现改写，见下一条）。未测行经用户接受、I11 解决后升回 settled，并恢复 _Check:_（[CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及热插拔/通知回归、`Tests/volume` 卷对账主机测试、拔插项）。
- 2026-11-16 · decision · STOR-8 · 用户批准按实现改写失败约定（卷变了才不算文件错误；卷没变按读错误处理，读取任务有 100 ms 卷宽限）
- 2026-11-05 · friction · STOR-8 · 暂停状态下 30 ms 内快速抽插走 `gate_only`：设备仍是 READY、刷新没有重新初始化，对账只开闸，卡若真掉过电，要到下一次读才报错、再拔一次才恢复（#195 “假抖动只开闸”的取舍，没改）。第 7 次板测 R3.4 用假抖动钩子 30 次 `gate_only` 无停止、欠载 0；真人 30 ms 内抽插做不到，常见与否未知。若日后实测常见，评估把“暂停期间发生过边沿”也当作换卡信号。
