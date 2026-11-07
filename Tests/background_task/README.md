# 后台任务主机测试

本目录验证 `APP/tasks/background/` 里能在主机上跑的部分（#213、#220，ARC-22）。四个测试程序（压力钩子一份源码编四个配置），42 个 CTest：

| 程序 | CTest | 链接的生产代码 | 替身 |
|---|---|---|---|
| `background_loop_tests` | `background_loop`（1 个） | `background_loop.c`、`volume_event.c` | 共用 FreeRTOS 桩（任务通知、tick、阻塞钩子） |
| `background_reclaim_tests` | `background_reclaim_*`（9 个，每个用例一个进程） | `background_reclaim.c`、`volume_event.c` | 共用桩、Service Log 替身、**`Service_Filesystem_ReclaimFlash` 在测试文件里用脚本化的假实现**（脚本给出每次调用的返回状态，记录调用时刻） |
| `background_pressure_{default,hook,floor,noreclaim}_tests` | `background_pressure_*`（27 个）、`background_registry_*`（4 个），每个用例一个进程 | `background_flash_pressure.c`、`background_jobs.c`、`background_reclaim.c`、`background_loop.c`、`volume_event.c` | 共用桩、Service Log 替身（`Tests/stubs/lock_env`）、**Filesystem 的五个文件入口（RemoveFile、OpenFile、WriteFile、CloseFile、ReclaimFlash）和 `Platform_Flash_GetVolumeDiagnostics` 在测试文件里用脚本化的假实现**（假 FTL：空闲块、水位以下回收一块计一次擦除、耗尽时写入自己擦） |
| `task_priorities_tests` | `task_priorities`（1 个） | 只包含 `APP/app_config.h`，全是 `_Static_assert` | 无 |

压力钩子的四个配置：默认（钩子关闭）、`BACKGROUND_HOOK_FLASH_PRESSURE=1` 加 `RECLAIM=1`（覆盖钩子自己的回收分支）、再加 `FREE_FLOOR=300`、钩子打开但 `RECLAIM` 用默认值（`_Static_assert` 核对默认是 0：产品的回收由回收作业负责，#220）。作业状态和卷事件是静态的，每个用例一个进程（命令行参数选用例）。

## 覆盖

- **循环**（期望值来自 #202 F 节的字面描述，不用代码里的表达式重算）：每圈每个作业恰好调一次，按表的顺序，第一个返回 0 也不短路；睡眠时长是最小返回值，0 最小，等通知哨兵最大，表为空或全是空项是等通知；毫秒换 tick（0、1、整数、哨兵、哨兵减 1）；没人叫醒睡满最小等待，全部等通知是无限等待；睡眠期间有人 `background_task_wake()` 提前醒，无限等待也叫得醒；作业运行期间到的通知不丢（睡眠立刻结束，下一圈再睡满）；连叫几次合并成一次；wake 落在约定的槽和标志位，句柄未绑定时是空操作；**卷事件（`volume_publish_ready/gone`，任务入口登记了订阅）落在同一个槽的 VOLUME 位上，叫醒睡眠，醒来时这一位被一次取走，下一圈不再被旧事件叫醒**。
- **回收作业**（#220，STOR-12、#196 第 18、19 条）：Flash 卷序号为 0 直接返回（`BACKGROUND_WAIT_FOREVER`，不调回收、不记日志，时间过去多久都一样）；第一次回收机会在看到新卷序号之后一个周期（100 ms），之前只回报还剩多久；一次最多一块（落后很多个周期也只调一次，同一时刻再叫不再调）；被别的作业或通知频繁叫醒时周期不变（每 10 ms 调一次作业，1 s 内恰好 10 次回收、间隔 100 ms）；卷没挂载（卷消失事件之后）不调用，重新挂载后一个周期才给机会；失败一次就停（`SERVICE_ERROR`、`SERVICE_TIMEOUT`）：一条 ERROR（标签 `FLASH`，`Reclaim failed status=<数值>, stopped until Flash volume sequence changes`，不超过 127 字符），之后无论过多久都不再调用、不再记日志；卷不可用类（`NO_MEDIA`、`NOT_READY`、`INVALID_HANDLE`）和 `SERVICE_BUSY` 不算失败，下个周期照常；卷序号变化（卸载再挂载）后恢复，不再记旧的失败。
- **作业表**：内置作业按序在前（回收永远在，压力钩子关闭时没有这一项），注册表各应用的 `background_step` 按表序在后，空字段不占位；没有应用登记后台作业时只剩内置作业；默认构建里 Flash 卷没发过事件时整圈都是等通知。
- **压力钩子**：默认构建没有压力钩子的作业，也没有文件调用和日志；钩子打开时作业表里回收在前、压力钩子在后，启动日志一行 WARN `HOOK: flash_pressure ON …`（三个调参配置的字面值，默认 `reclaim=0`）；Flash 卷没就绪时不调任何文件接口，只每 500 ms 看一次，就绪后才开始，卷消失立刻停手；一个文件的一圈调用序列（删遗留、新建、128 次“写、回收”、关闭、删除、新建），每步返回值；写入内容的来源地址与每块 32768 字节、一个文件正好 4 MiB；统计行格式与节拍（`start` 行字面值、60 s 内 5 至 6 行 `run`、空闲块与擦除次数）；空闲块压到 462 以下后出现真擦除，日志里的数字与假 FTL 一致、擦除次数是钩子开始之后的增量；卷满（字节数不足、`NO_SPACE`）关闭并删除该文件、只告警一次；`NOT_READY` 当卷不可用、不报错、不关旧句柄、卷序号变化后重新开始；卷序号在两步之间变了同样重新开始；写入失败、回收失败、新建失败（含 BUSY 重试用尽）、关闭失败、删除失败各一次就停（ERROR 行含步骤名与状态数值），关文件、不删除，停下后不再调文件接口，卷序号变化才恢复；新建 BUSY 先删遗留再重试；诊断读取失败不影响作业；空闲块下限暂停写入、只靠回收抬回来、写入随后继续；钩子的回收默认关闭时一次回收都不调；**每一步最多一次文件调用**（驱动函数对每一步断言）。
- **优先级排序**（编译期，`test_task_priorities.c`）：播放 4 > 音频读取、卷 3 > 前台、GUI、Log、Monitor 2 > 后台、Flash 启动 1 > Boot 0，Flash 启动任务与后台任务同级 1（#220 方案 A）；Boot 栈 192 word。

## 保真边界（HAR-5）

PASS 证明：每圈作业各一次与最小等待的判断对；通知的槽、位、合并与“不丢”对；钩子作业每一步的调用顺序、返回值、日志格式和停下条件对；回收作业的周期、一次一块、卷序号为 0 与卷未挂载不调用、失败一次就停和恢复对；作业表的拼法对；优先级排序对；默认构建里没有压力钩子。

**主机测试是单线程的，通过不等于并发没问题。** 不证明：真实的任务切换、优先级继承、`xTaskNotifyWaitIndexed` 在真实内核里的睡眠与醒来延迟；后台任务与 GUI 同级时实际分到多少 CPU；**后台任务与 Flash 启动任务同级时的真实轮转和开机扫描时长**；播放中真擦除时欠载是否为 0；真实的 Flash 持锁时长、擦除是否真的发生、空闲块真的降到水位以下要多久（日志里 `free`、`erases`）；SD 与 Flash 同时进行时互不影响（R4.1、R4.2、R6.3）；假 FTL 只有三行模型，不是 FTL；任务栈余量（R6.8）；`background_task.c`（静态创建、`for(;;)`）没有主机测试，靠固件构建和上板核对。这些结论在 [后台任务 README](../../APP/tasks/background/README.md) 里同样列出。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module background_task
```
