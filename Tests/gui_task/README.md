# GUI Task 主机测试

本目录验证三块与 LVGL 无关的逻辑：

- `gui_music_queue_window.h`：Queue 窗口 request / APPLY / CLEAR 与滚动换窗 Index，以及 APPLY 记下的已显示窗口代次；
- `gui_music_transport.h`：播放快照到显示决定的纯 C 归约（冻结与序号、Queue 高亮与队列满撤回、进度与时间、停止短语、跳过/停止计数、唱盘归零）和命令编号；用假的 `playback_command_send()` 替身，只经合同头 `playback_contract.h`；
- `gui_service_input.h`：点击单槽 Consume，空闲为 `NONE`，SEEK 携带 0..100。

不链接 LVGL、FreeRTOS、HAL、播放任务。Host PASS 不证明真机跟手滚动、换标或插拔时序。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module gui_task
```
