# GUI Task 主机测试

本目录验证四块与 LVGL 无关的逻辑：

- `gui_music_queue_window.h`：Queue 窗口 request / APPLY / CLEAR 与滚动换窗 Index，以及 APPLY 记下的已显示窗口代次；
- `gui_music_transport.h`：播放快照到显示决定的纯 C 归约（冻结与序号、Queue 高亮与队列满撤回、进度与时间、停止短语、跳过/停止计数、唱盘归零）和命令编号，以及接线后 `gui_music.c` 依赖的快照序列回归（开机开播时代次确认、回显落后时换窗的高亮、队列满撤回后追上）；用假的 `playback_command_send()` 替身，只经合同头 `playback_contract.h`；
- `gui_music_title.h`：曲名客户——无当前曲显示 `--` 不请求、有当前曲留空并按其身份请求、PENDING 期间不重复请求、同一身份的有效应答采信为名字、应答代次 0 显示 `--`、身份变化即清空并按新身份请求、在途期间身份变化时丢弃旧应答再重请求、失去当前曲显示 `--`、槽拒绝请求后重试；归约测试另断言决定给出的曲目身份未追上时冻结；
- `gui_service_input.h`：点击单槽 Consume，空闲为 `NONE`，SEEK 携带 0..100。

不链接 LVGL、FreeRTOS、HAL、播放任务；`gui_music.c` 的编排由 GUI 模拟器场景覆盖。Host PASS 不证明真机跟手滚动、换标或插拔时序。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module gui_task
```
