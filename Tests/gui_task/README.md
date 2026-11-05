# GUI Task 主机测试

本目录验证一块与 LVGL 无关的逻辑：

- `gui_service_input.h`：点击单槽 Consume，空闲为 `NONE`，`SEEK_RELEASE` 携带千分比；未取走的 `SEEK_PRESS` 遇到 `SEEK_DRAG` 时保留按下、取拖动的值。

不链接 LVGL、FreeRTOS、HAL。音乐应用 GUI 分区的主机测试在 [`Tests/music_gui`](../music_gui/README.md)。Host PASS 不证明真机触摸与 GUI 刷新时序。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module gui_task
```
