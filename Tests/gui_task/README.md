# GUI Task 主机测试

本目录通过 `APP/tasks/gui/gui_task_queue_window.h` 验证 GUI Task 对 `storage_listbuffer` 窗口的 request / APPLY / CLEAR 时机、按滚动整行数计算下一窗 `Index`，以及按游标代次决定当前行。只编译状态机源文件，不链接 LVGL、FreeRTOS、HAL 或 `Service/gui`。Host PASS 不证明真机 Queue 跟手滚动与插拔时序。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module gui_task
```
