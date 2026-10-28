# GUI Task 主机测试

本目录验证五块与 LVGL 无关的逻辑：

- `gui_music_queue_window.h`：Queue 窗口 request / APPLY / CLEAR 与滚动换窗 Index，以及 APPLY 记下的已显示窗口代次；
- `gui_music_transport.h`：播放快照到显示决定的纯 C 归约（冻结与序号、Queue 高亮与队列满撤回、进度与时间、停止短语、跳过/停止计数、唱盘归零）和命令编号，以及接线后 `gui_music.c` 依赖的快照序列回归（开机开播时代次确认、回显落后时换窗的高亮、队列满撤回后追上）；用假的 `playback_command_send()` 替身，只经合同头 `playback_contract.h`；
- `gui_music_title.h`：曲名客户——无当前曲显示 `--` 不请求、有当前曲留空并按其身份请求、PENDING 期间不重复请求、同一身份的有效应答采信为名字、应答代次 0 显示 `--`、身份变化即清空并按新身份请求、在途期间身份变化时丢弃旧应答再重请求、连按 NEXT 只请求并采信最后一首、坏文件跳过按候选请求、在途期间失去当前曲显示 `--` 且不再请求、失去当前曲显示 `--`、槽拒绝请求后重试、身份冻结时名字保持不请求；冷启动代次 0 → G 时已显示名字的应答代次等于 G 则保留、不等则重请求，代次 0 的请求在途时应答代次等于 G 采信、否则丢弃并按 G 重请求；归约测试另断言决定给出的曲目身份未追上时冻结；
- `gui_music_queue_rows.h`：Queue 窗口客户把 READY 窗口槽的两列文本按行交出——曲名与歌手平行（第 i 项同属窗口第 i 行）、歌手为空串的行交出 `Unknown Artist`（GUI-25，不是空串、NULL 或 `--`），窗口槽里的歌手仍是空串、`Length` 超容量时截到容量、空窗 0 行；只按窗口槽头构造窗口，不链接 Storage 实现；`QueueApply` 把歌手画进行由模拟器场景 `queue_tags`、`queue_track_text` 覆盖；
- `gui_service_input.h`：点击单槽 Consume，空闲为 `NONE`，SEEK 携带 0..100。

不链接 LVGL、FreeRTOS、HAL、播放任务；`gui_music.c` 的编排由 GUI 模拟器场景覆盖。Host PASS 不证明真机跟手滚动、换标或插拔时序。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module gui_task
```
