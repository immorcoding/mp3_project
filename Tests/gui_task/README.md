# GUI Task 主机测试

本目录验证一块与 LVGL 无关的逻辑：

- `gui_service_input.h`：点击单槽 Consume，空闲为 `NONE`，`SEEK_RELEASE` 携带千分比；未取走的 `SEEK_PRESS` 遇到 `SEEK_DRAG` 时保留按下、取拖动的值；`service_gui_input_has_pending()` 随 post、Consume、丢弃 Queue 选曲同步变化。
- `gui_service_sleep.h`（`gui_service_sleep_tests`）：GUI 每圈睡眠时长（GUI-28），时长 = 定时器返回值 - 本圈耗时，夹在 1 ms 与 16 ms 之间；耗时超过返回值不因无符号相减回绕；`LV_NO_TIMER_READY`（0xFFFFFFFF）落在上限；有待取走的输入时只睡下限；在一组边界输入上结果恒在 1 到 16 ms。期望值来自 #190 的决定，不是从实现反推。

不链接 LVGL、FreeRTOS、HAL。音乐应用 GUI 分区的主机测试在 [`Tests/music_gui`](../music_gui/README.md)。Host PASS 不证明真机触摸与 GUI 刷新时序；睡眠测试也不证明 `Service_GUI_Process()` 传入的本圈耗时与待取标志取得对（那段含 LVGL 与 FreeRTOS，不在主机工程里），也不证明真实节拍、CPU 空闲比例与松手到出声时延，这些靠板测。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module gui_task
```
