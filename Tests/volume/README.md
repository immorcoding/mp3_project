# 卷任务主机测试

本目录验证 `APP/tasks/volume/` 里能在主机上跑的部分（#211，STOR-8、STOR-9，ADR-0024 的“卷生命周期”）。四个测试程序，14 个 CTest：

| 程序 | CTest | 链接的生产代码 | 替身 |
|---|---|---|---|
| `volume_reconcile_tests` | `volume_reconcile` | `volume_reconcile.c`（纯 C） | 只借共用桩的头取 `TickType_t` |
| `volume_event_tests` | `volume_event_<用例>` ×9 | `volume_event.c` | 共用 FreeRTOS 桩（假任务与通知计数、通知钩子） |
| `volume_sd_default_tests`、`volume_sd_benchmark_tests`、`volume_sd_hook_tests` | `volume_sd`、`volume_sd_benchmark_order`、`volume_sd_fake_bounce_hook` | `volume_sd.c`、`volume_reconcile.c`、`volume_event.c` | 共用 FreeRTOS 桩、Service Log 替身（`Tests/stubs/lock_env`）、**Filesystem Service 的七个入口在测试文件里用脚本化的假实现**（StartSD、RefreshSD、PauseSD、ResumeSD、InitSD、MountSD、UnmountSD） |
| `volume_flash_tests` | `volume_flash` | `volume_flash.c`、`volume_event.c` | 共用桩、Service Log 替身、Filesystem 的 InitFlash、MountFlash、FormatAndMountFlash 假实现 |

`volume_sd` 的同一份测试源编三次：默认配置；`STORAGE_SD_BENCHMARK_ENABLE=1`（验证 SD 基准在发卷就绪之前运行：此时卷已挂载，卷序号还没变）；`VOLUME_HOOK_FAKE_BOUNCE=1`（验证假抖动钩子的 HOOK 行与节拍；钩子的节拍锚在启动的 tick，别的用例用的假时间会被它当成到期，所以这个配置只跑钩子相关的两个用例）。

## 覆盖

- **对账表**（`volume_reconcile`）：#195 的 5 行表每行一例；刷新失败时卷挂着一律卸载、卷没挂一律无动作（不会因为结构里写着“就绪”就去挂载）；路径名（日志 `path=` 的取值，R3.1 至 R3.5 判读依据）；静默时间的剩余随 tick 递减、新边沿重新计时、tick 回绕时仍对。期望值来自 #195 的表，不用代码里的表达式重算。
- **卷事件**（`volume_event`，每个用例一个进程，因为模块状态是静态的、没有也不该有测试用的复位入口）：（#215 新增 `published_edge`：边沿时刻随它所属的事件发布，叫醒的那一刻读得到，换卡的卸载与挂载共用同一个边沿，开机挂载没有边沿读不到，tick 0 是合法时刻，另一个卷和越界读不到；`volume_sd` 的拔卡和换卡用例也核对它等于第一个边沿的 tick。读两次状态字夹边沿的防撕裂只能靠读代码，单线程测不到。）开机状态；序号逐次加 1、两个卷互相独立；通知落在订阅者登记的槽和标志位上；**先更新再叫醒**（桩的通知钩子在叫醒那一刻读卷状态字，序号和挂载位已是新值）；换卡序号跳 2、挂载位保持、订阅者收到三次通知（初次就绪、消失、再就绪）；**晚登记不漏**（登记前发布了三个事件，登记不补叫醒，但登记后比对序号看得到“变过、现在挂着”）；订阅者上限（恰好上限个成功，再多一个返回假，已登记的各被叫醒一次，被拒的一次都没有）；重复登记幂等、空任务被拒。
- **SD 部分**（`volume_sd`）：
  - 启动：有卡则 StartSD → InitSD → MountSD → 发卷就绪（序号 0 到 1，启动路径没有闸）；无卡不发事件；挂载失败（无 FAT、出错、忙）、驱动没就绪、卡在但没就绪、Platform 启动失败都不发事件。
  - 5 行表的每条路径（`unmount`、`remount`、`mount`、`idle`、`gate_only`），断言**调用顺序**（`PRUO`、`PRUIMO`、`PRIMO`、`PRO`：关闸 → 刷新 → 卸载 → 初始化 → 挂载 → 开闸）与每一步那一刻的卷状态字：卸载时卷状态字还没改（先卸载后发），挂载时卷消失已发布，开闸时卷就绪已发布，订阅者被叫醒时闸始终关着（最后才开闸，也不等订阅者）。
  - 挂载失败不发卷就绪，闸照样打开；换卡路径上挂载失败卷消失照发；卸载返回错误照发卷消失；刷新失败（卡不在、卡在而设备 ERROR）卸载旧卷，刷新失败而卷没挂不会挂载。
  - 抖动：边沿重新计时、闸只关一次、边沿日志只记一行、对账日志记边沿数；日志里的 `ref=`、`end=` 是毫秒数（刷新花 186 ms 时 `end=216ms`）；对账期间（刷新、卸载、挂载处）来的新边沿让闸保持关闭、重新消抖，第二次对账看到稳定现状之后才开闸，第一次该做的事不重复做。
  - **没有路径漏开闸**：`test_every_path_pairs_pause_with_resume` 穷举 2 × 4 × 2 × 3 × 2 × 4 = 384 种组合（卷挂没挂、卡的四种现状、刷新成败、挂载三种结果、卸载成败、在刷新/卸载/挂载处来不来新边沿），每种的收尾都是闸开着、`PauseSD` 与 `ResumeSD` 各一次、卷状态字与假 Service 眼里的挂载状态一致、卷序号的变化与表一致、订阅者被叫醒时闸关着；过程中卷任务只要还在消抖，`volume_sd_step()` 返回的就是有限等待。
  - 日志行不超过 Service Log 的 127 字符（12345 个边沿、刷新花 123456 ms、时钟在回绕边上，字段被截在 9999）。
  - ISR 回调只发通知（对卷任务的通知槽置检测位，把唤醒标志交给 `portYIELD_FROM_ISR`），空上下文什么都不做。
  - 假抖动钩子：默认配置里没有 HOOK 行，过 10 小时也不自己造边沿、不动闸；打开时启动一行 WARN `HOOK: fake_bounce ON period=5000ms`，空闲等待被钩子的下一次到期限制，到期后造一次边沿，走 `gate_only`、卷状态字不变、订阅者不被叫醒、日志写明 `src=hook`。
- **Flash 启动流程**（`volume_flash`）：挂载成功才发 Flash 的卷就绪（序号 0 到 1，日志写 `path=mount` 和耗时毫秒）；无文件系统按 AUTO_FORMAT 格式化并挂载，格式化那一刻卷序号还是原值；格式化失败、其他挂载失败（出错、忙、超时、没就绪）、执行器初始化失败都不发事件，且其他失败不触发格式化；只动 Flash 的字，不动 SD 的字；日志行不超过 127 字符。

## 保真边界（HAR-5）

PASS 证明每条路径上**调用的顺序、卷状态字的更新时机、闸的开合、发不发事件**对，以及纯函数和日志格式对。

**主机测试是单线程的，通过不等于并发没问题。** 不证明：真实的任务抢占与优先级继承；闸开时多个等待者按优先级怎样醒；订阅者与发布者的真实交错；卡检测中断的优先级与真实时序；边沿到对账的真实时延（R3.1 的收尾时刻）；Flash 启动任务与 SD 初始化真实重叠时的时序（R3.5）；两个新任务的栈余量（R3.6）；`MountFlash` 的实际耗时；`Platform_SD_*` 和真实 Abort（Filesystem 的入口在这里是脚本化的替身，真实的 Service 加 Platform 加 Component 在 `Tests/flash_ftl` 的 `sd_removal_chain` 里与同一份 `volume_sd.c` 对接）。`volume_task.c`（`for(;;)` 等待）与 `volume_flash_task.c`（静态创建、栈水位、自删）没有主机测试，靠固件构建和第 3 次上板。这些结论在 [卷任务 README](../../APP/tasks/volume/README.md) 的“主机测试证明什么、不证明什么”里同样列出。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module volume
```
