# GUI Task 主机测试

本目录验证三块与 LVGL 无关的逻辑：

- `gui_music_queue_window.h`：Queue 窗口 request / APPLY / CLEAR 与滚动换窗 Index；
- `gui_music_transport.h`：paused/playing 与假进度策略；
- `gui_service_input.h`：点击单槽 Consume，空闲为 `NONE`，SEEK 携带 0..100。

不链接 LVGL、FreeRTOS、HAL。Host PASS 不证明真机跟手滚动、换标或插拔时序。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module gui_task
```
