# Monitor 主机测试

本目录有两个纯 C 测试程序，都只编译被测单元，不链接 FreeRTOS、Service_Log 或 HAL。

**`monitor_warn_tests`**（CTest `monitor_warn_dedup`）验证 `APP/tasks/monitor/monitor_warn.c` 的告警去重记录：同名只告警一次、记录写满后新名字保持沉默且不越界、超长名字安全截断、空输入被忽略。Host PASS 不证明板上 MONITOR 日志里确实出现告警行。

**`monitor_stats_tests`**（CTest `monitor_stats`，DIAG-6、#204）验证 `APP/tasks/monitor/monitor_stats.c` 的 5 s 批次计算与成行：

- `CPU` 行：首个窗口从启动算起；之后的窗口只看两次快照的差值；运行计数与总计数 32 位回绕时占比仍对；基准里没有的新任务按 0 起算、本次没有的任务不出现；总计数没有前进时输出 `CPU unavailable` 而不除零；按键的字典序排列；缓冲放不下时末字符为 `~` 且不越界；基准写满后多出的任务不记录。
- `Health` 行：累计值与本窗口增量；全零时也输出；累计值回绕时增量仍是真实差值；三项都取 32 位最大值时仍放进 Service Log 的单条 127 字符。

期望值都是手算的字面量（例如 9600 个计数里 IDLE 占 8630 为 `89.8`），不用被测代码的公式重算。

保真边界：PASS 只证明算术与格式。`monitor_task.c` 外壳（取 `uxTaskGetSystemState` 快照、取欠载与日志丢失累计、投递、静态缓冲）无主机测试，靠固件构建与板上日志；`Service_Log_GetDropStats()` 的计数也无主机测试（它依赖 FreeRTOS 队列与临界区）；5 s 一批是否放得进 30 块消息池靠 `monitor_task.c` 的 `_Static_assert` 与 README 的核算，实际有无丢失看板上 `Health` 行。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module monitor_warn
```
