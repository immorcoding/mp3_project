# Monitor 未登记任务告警主机测试

本目录验证 `APP/tasks/monitor/monitor_warn.c` 的告警去重记录：同名只告警一次、记录写满后新名字保持沉默且不越界、超长名字安全截断、空输入被忽略。只编译该纯 C 单元，不链接 FreeRTOS、Service_Log 或 HAL。Host PASS 不证明板上 MONITOR 日志里确实出现告警行。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module monitor_warn
```
