# 前台任务主机测试

本目录验证前台任务一圈的判断（`APP/tasks/foreground/foreground_loop.c`，ARC-26，#218）和注册表 `boot_init` 的逐行遍历（`APP/apps/app_table_boot.c`）。测试链接真实的 `foreground_loop.c`、`app_table_boot.c`、共用 FreeRTOS 桩（`Tests/stubs/freertos/`，任务通知与 tick）以及脚本化的周期计数器和 Service Log 替身（`Tests/stubs/lock_env/`），把各应用的 `foreground_step` 换成测试里的脚本化函数，只经公开函数观察。`foreground_task.c`（静态创建与任务入口）没有主机测试，由固件构建和第 5 次上板覆盖。

一个 CTest：`foreground_loop`（14 个用例，同一个进程）。覆盖：

- 每圈每个应用的 step 恰好一次、按表里的顺序，返回 0 也不让后面的应用被跳过，空字段（`NULL`）跳过；
- 睡眠时长取各 step 返回值的最小值，没有 step 或都返回“一直等”时等通知；毫秒换 tick，非零的毫秒不会变成 0 tick；
- 一圈是“调 step、调每圈回调、再等通知”，回调先于等待；
- 睡眠期间和 step 运行期间的叫醒都不丢（通知留在槽里，等待立刻返回）；任务句柄还没绑定时 `foreground_task_wake()` 是空操作；
- 每个应用各记自己的最长耗时，只升不降；超过阈值（50 ms）且刷新了最长值才记一行 WARN，恰好等于阈值、与上次相等、更短都不记，再刷新再记，step 的返回值照常使用；
- WARN 行和汇总行在长应用名下也不超过 127 字符，汇总行放不下时末字符是 `~`；汇总至多每 10 s 一行，没有新刷新不记；
- 周期计数器没启动（频率 0）时耗时记 0，不误报；32 位计数回绕不让耗时虚高；
- `app_table_boot_init_rows()` 按表的顺序逐行调用 `boot_init`，空字段跳过。

保真边界：主机测试是单线程的，PASS 证明各分支上的判断正确，不证明真实任务切换与优先级继承、通知唤醒延迟、与 GUI 同级时的 CPU 份额、step 的真实耗时（耗时用脚本化计数器注入）、前台任务的栈余量和 Boot 栈余量（后者是静态核算，见 [前台任务 README](../../APP/tasks/foreground/README.md)）。这些进板级验收（清单 3 第 5 次）。音乐前台流程见 [Tests/music_foreground](../music_foreground/README.md)。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module foreground_task
```
