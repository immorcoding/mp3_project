# GUI Canvas 主机测试

本目录验证 `canvas/` compositor 把唱盘圆与中心假封面圆写进调用方缓冲。只编译 compositor 与最小 `lvgl.h` 替身，不链接 LVGL、FreeRTOS 或 HAL。Host PASS 不证明真机 Image 绑定与旋转。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module gui_canvas
```
